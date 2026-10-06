#include "toml.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace toml {

namespace {

enum class ValueKind {
    String,
    Boolean,
    List,
};

struct Value {
    ValueKind kind = ValueKind::String;
    std::string string;
    bool boolean = false;
    std::vector<Value> list;

    static Value string_value(std::string value) {
        Value result;
        result.kind = ValueKind::String;
        result.string = std::move(value);
        return result;
    }

    static Value boolean_value(bool value) {
        Value result;
        result.kind = ValueKind::Boolean;
        result.boolean = value;
        return result;
    }

    static Value list_value(std::vector<Value> value) {
        Value result;
        result.kind = ValueKind::List;
        result.list = std::move(value);
        return result;
    }
};

struct ParsedManifest {
    Project project;
    std::vector<std::string> imports;
    std::string canonical_path;
    std::string raw_contents;
};

struct SourceLine {
    std::string text;
    std::size_t number = 0;
    std::size_t indent = 0;
};

std::string trim(const std::string& value) {
    std::size_t first = 0;

    while (first < value.size() &&
           std::isspace(
               static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    std::size_t last = value.size();

    while (last > first &&
           std::isspace(
               static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }

    return value.substr(first, last - first);
}

std::string strip_comment(const std::string& input) {
    bool in_string = false;
    bool escaped = false;

    for (std::size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];

        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }

            continue;
        }

        if (c == '"') {
            in_string = true;
            continue;
        }

        if (c == '#') {
            return input.substr(0, i);
        }
    }

    return input;
}

bool is_identifier_start(char c) {
    return std::isalpha(
               static_cast<unsigned char>(c)) ||
           c == '_';
}

bool is_identifier_char(char c) {
    return std::isalnum(
               static_cast<unsigned char>(c)) ||
           c == '_';
}

bool is_identifier(const std::string& value) {
    if (value.empty() ||
        !is_identifier_start(value.front())) {
        return false;
    }

    for (std::size_t i = 1; i < value.size(); ++i) {
        if (!is_identifier_char(value[i])) {
            return false;
        }
    }

    return true;
}

std::size_t find_top_level(
    const std::string& value,
    const std::string& needle) {

    bool in_string = false;
    bool escaped = false;
    int depth = 0;

    for (std::size_t i = 0;
         i + needle.size() <= value.size();
         ++i) {

        const char c = value[i];

        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }

            continue;
        }

        if (c == '"') {
            in_string = true;
            continue;
        }

        if (c == '[' || c == '(') {
            ++depth;
            continue;
        }

        if (c == ']' || c == ')') {
            --depth;
            continue;
        }

        if (depth == 0 &&
            value.compare(i, needle.size(), needle) == 0) {
            return i;
        }
    }

    return std::string::npos;
}

std::vector<std::string> split_top_level(
    const std::string& value,
    char delimiter) {

    std::vector<std::string> result;
    std::string current;

    bool in_string = false;
    bool escaped = false;
    int depth = 0;

    for (char c : value) {
        if (in_string) {
            current.push_back(c);

            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }

            continue;
        }

        if (c == '"') {
            in_string = true;
            current.push_back(c);
            continue;
        }

        if (c == '[' || c == '(') {
            ++depth;
            current.push_back(c);
            continue;
        }

        if (c == ']' || c == ')') {
            --depth;
            current.push_back(c);
            continue;
        }

        if (c == delimiter && depth == 0) {
            result.push_back(trim(current));
            current.clear();
            continue;
        }

        current.push_back(c);
    }

    if (!trim(current).empty()) {
        result.push_back(trim(current));
    }

    return result;
}

std::string parse_string(
    const std::string& value,
    const std::string& context) {

    const std::string s = trim(value);

    if (s.size() < 2 ||
        s.front() != '"' ||
        s.back() != '"') {
        throw std::runtime_error(
            "expected quoted string in " +
            context);
    }

    std::string result;
    result.reserve(s.size() - 2);

    bool escaped = false;

    for (std::size_t i = 1;
         i + 1 < s.size();
         ++i) {

        const char c = s[i];

        if (escaped) {
            switch (c) {
                case '"':
                    result.push_back('"');
                    break;

                case '\\':
                    result.push_back('\\');
                    break;

                case 'n':
                    result.push_back('\n');
                    break;

                case 't':
                    result.push_back('\t');
                    break;

                default:
                    throw std::runtime_error(
                        "unsupported escape sequence \\" +
                        std::string(1, c) +
                        " in " +
                        context);
            }

            escaped = false;
            continue;
        }

        if (c == '\\') {
            escaped = true;
            continue;
        }

        result.push_back(c);
    }

    if (escaped) {
        throw std::runtime_error(
            "unterminated escape sequence in " +
            context);
    }

    return result;
}

bool values_equal(
    const Value& left,
    const Value& right) {

    if (left.kind != right.kind) {
        return false;
    }

    if (left.kind == ValueKind::String) {
        return left.string == right.string;
    }

    if (left.kind == ValueKind::Boolean) {
        return left.boolean == right.boolean;
    }

    if (left.list.size() != right.list.size()) {
        return false;
    }

    for (std::size_t i = 0;
         i < left.list.size();
         ++i) {

        if (!values_equal(
                left.list[i],
                right.list[i])) {
            return false;
        }
    }

    return true;
}

struct ParserState {
    std::unordered_map<std::string, Value> variables;
    std::unordered_set<std::string> declared_args;
    std::unordered_set<std::string> parsed_args;
    std::unordered_set<std::string> overridden_args;
    bool discovering_args = false;
};

struct Parser {
    fs::path path;
    std::vector<SourceLine> lines;
    std::unordered_map<std::string, Value>& variables;
    std::unordered_set<std::string>& declared_args;
    std::unordered_set<std::string>& parsed_args;
    std::unordered_set<std::string>& overridden_args;
    bool& discovering_args;

    explicit Parser(
        const fs::path& manifest,
        const std::string& contents,
        ParserState& state)
        : path(manifest),
          variables(state.variables),
          declared_args(state.declared_args),
          parsed_args(state.parsed_args),
          overridden_args(state.overridden_args),
          discovering_args(state.discovering_args) {

        std::istringstream input(contents);
        std::string physical;
        std::size_t line_number = 0;

        while (std::getline(input, physical)) {
            ++line_number;

            if (!physical.empty() &&
                physical.back() == '\r') {
                physical.pop_back();
            }

            std::size_t indent = 0;

            while (indent < physical.size() &&
                   (physical[indent] == ' ' ||
                    physical[indent] == '\t')) {

                if (physical[indent] == '\t') {
                    throw std::runtime_error(
                        "tabs are not allowed for indentation in " +
                        manifest.string() +
                        ":" +
                        std::to_string(line_number));
                }

                ++indent;
            }

            std::string text =
                strip_comment(
                    physical.substr(indent));

            text = trim(text);

            if (text.empty()) {
                continue;
            }

            SourceLine line;
            line.text = std::move(text);
            line.number = line_number;
            line.indent = indent;

            lines.push_back(std::move(line));
        }
    }

    std::string context(std::size_t line) const {
        return path.string() +
               ":" +
               std::to_string(line);
    }

    [[noreturn]]
    void error(
        std::size_t line,
        const std::string& message) const {

        throw std::runtime_error(
            message +
            " in " +
            context(line));
    }

    std::pair<std::string, std::size_t> collect_value(
        std::size_t index,
        std::size_t end,
        const std::string& initial,
        std::size_t line) const {

        std::string value = initial;

        int bracket_depth = 0;
        bool in_string = false;
        bool escaped = false;

        auto scan = [&](const std::string& text) {
            for (char c : text) {
                if (in_string) {
                    if (escaped) {
                        escaped = false;
                    } else if (c == '\\') {
                        escaped = true;
                    } else if (c == '"') {
                        in_string = false;
                    }

                    continue;
                }

                if (c == '"') {
                    in_string = true;
                } else if (c == '[') {
                    ++bracket_depth;
                } else if (c == ']') {
                    --bracket_depth;
                }
            }
        };

        scan(value);

        std::size_t next = index + 1;

        while (bracket_depth > 0) {
            if (next >= end) {
                error(
                    line,
                    "unterminated list");
            }

            value += " ";
            value += lines[next].text;

            scan(lines[next].text);
            ++next;
        }

        return {value, next};
    }

    Value parse_value(
        const std::string& raw,
        std::size_t line) {

        const std::string value = trim(raw);

        if (value.empty()) {
            error(
                line,
                "expected value");
        }

        if (value == "true" ||
            value == "True") {
            return Value::boolean_value(true);
        }

        if (value == "false" ||
            value == "False") {
            return Value::boolean_value(false);
        }

        if (value.front() == '"') {
            return Value::string_value(
                parse_string(
                    value,
                    context(line)));
        }

        if (value.front() == '[') {
            if (value.back() != ']') {
                error(
                    line,
                    "unterminated list");
            }

            const std::string body =
                trim(
                    value.substr(
                        1,
                        value.size() - 2));

            std::vector<Value> result;

            if (!body.empty()) {
                for (const std::string& item :
                     split_top_level(body, ',')) {

                    result.push_back(
                        parse_value(
                            item,
                            line));
                }
            }

            return Value::list_value(
                std::move(result));
        }

        if (is_identifier(value)) {
            auto it =
                variables.find(value);

            if (it == variables.end()) {
                error(
                    line,
                    "unknown variable '" +
                    value +
                    "'");
            }

            return it->second;
        }

        error(
            line,
            "unsupported value '" +
            value +
            "'");
    }

    std::vector<std::string> strings(
        const Value& value,
        std::size_t line) const {

        if (value.kind == ValueKind::String) {
            return {value.string};
        }

        if (value.kind != ValueKind::List) {
            error(
                line,
                "expected a string or list of strings");
        }

        std::vector<std::string> result;

        for (const Value& item : value.list) {
            if (item.kind != ValueKind::String) {
                error(
                    line,
                    "expected a list of strings");
            }

            result.push_back(item.string);
        }

        return result;
    }

    std::vector<std::vector<std::string>> action_lists(
        const Value& value,
        std::size_t line) const {

        if (value.kind != ValueKind::List) {
            error(
                line,
                "expected list of action lists");
        }

        std::vector<std::vector<std::string>> result;

        for (const Value& command : value.list) {
            result.push_back(
                strings(command, line));
        }

        return result;
    }

    bool condition(
        const std::string& expression,
        std::size_t line) {

        const std::string expr =
            trim(expression);

        if (expr.empty()) {
            error(
                line,
                "empty condition");
        }

        if (expr.front() == '(' &&
            expr.back() == ')') {

            int depth = 0;
            bool in_string = false;
            bool escaped = false;
            bool encloses_all = true;

            for (std::size_t i = 0;
                 i < expr.size();
                 ++i) {

                const char c = expr[i];

                if (in_string) {
                    if (escaped) {
                        escaped = false;
                    } else if (c == '\\') {
                        escaped = true;
                    } else if (c == '"') {
                        in_string = false;
                    }

                    continue;
                }

                if (c == '"') {
                    in_string = true;
                    continue;
                }

                if (c == '(') {
                    ++depth;
                } else if (c == ')') {
                    --depth;

                    if (depth == 0 &&
                        i != expr.size() - 1) {
                        encloses_all = false;
                        break;
                    }
                }
            }

            if (encloses_all) {
                return condition(
                    expr.substr(
                        1,
                        expr.size() - 2),
                    line);
            }
        }

        const std::size_t or_pos =
            find_top_level(
                expr,
                " or ");

        if (or_pos != std::string::npos) {
            return condition(
                       expr.substr(0, or_pos),
                       line) ||
                   condition(
                       expr.substr(or_pos + 4),
                       line);
        }

        const std::size_t and_pos =
            find_top_level(
                expr,
                " and ");

        if (and_pos != std::string::npos) {
            return condition(
                       expr.substr(0, and_pos),
                       line) &&
                   condition(
                       expr.substr(and_pos + 5),
                       line);
        }

        if (expr.size() >= 4 &&
            expr.compare(0, 4, "not ") == 0) {

            return !condition(
                expr.substr(4),
                line);
        }

        const std::size_t not_equal =
            find_top_level(
                expr,
                "!=");

        if (not_equal != std::string::npos) {
            const Value left =
                parse_value(
                    expr.substr(
                        0,
                        not_equal),
                    line);

            const Value right =
                parse_value(
                    expr.substr(
                        not_equal + 2),
                    line);

            return !values_equal(
                left,
                right);
        }

        const std::size_t equal =
            find_top_level(
                expr,
                "==");

        if (equal != std::string::npos) {
            const Value left =
                parse_value(
                    expr.substr(
                        0,
                        equal),
                    line);

            const Value right =
                parse_value(
                    expr.substr(
                        equal + 2),
                    line);

            return values_equal(
                left,
                right);
        }

        const Value value =
            parse_value(
                expr,
                line);

        if (value.kind != ValueKind::Boolean) {
            error(
                line,
                "condition must be boolean");
        }

        return value.boolean;
    }

    std::pair<std::string, std::string> assignment(
        const std::string& text,
        std::size_t line) const {

        const std::size_t append =
            find_top_level(
                text,
                "+=");

        if (append != std::string::npos) {
            return {
                trim(text.substr(0, append)),
                "+="
            };
        }

        const std::size_t equal =
            find_top_level(
                text,
                "=");

        if (equal != std::string::npos) {
            return {
                trim(text.substr(0, equal)),
                "="
            };
        }

        error(
            line,
            "expected assignment");
    }

    std::string assignment_value(
        const std::string& text,
        const std::string& operator_text,
        std::size_t line) const {

        const std::size_t position =
            find_top_level(
                text,
                operator_text);

        if (position == std::string::npos) {
            error(
                line,
                "invalid assignment");
        }

        const std::string value =
            trim(
                text.substr(
                    position +
                    operator_text.size()));

        if (value.empty()) {
            error(
                line,
                "missing assignment value");
        }

        return value;
    }

    void assign_variable(
        const std::string& name,
        const std::string& operator_text,
        const Value& value,
        std::size_t line) {

        if (!is_identifier(name)) {
            error(
                line,
                "invalid variable name '" +
                name +
                "'");
        }

        if (operator_text == "=") {
            variables[name] = value;
            return;
        }

        auto it =
            variables.find(name);

        if (it == variables.end()) {
            error(
                line,
                "cannot use += on undefined variable '" +
                name +
                "'");
        }

        if (it->second.kind != ValueKind::List) {
            error(
                line,
                "cannot use += on non-list variable '" +
                name +
                "'");
        }

        if (value.kind == ValueKind::List) {
            it->second.list.insert(
                it->second.list.end(),
                value.list.begin(),
                value.list.end());
        } else {
            it->second.list.push_back(value);
        }
    }

    void assign_target(
        Target& target,
        const std::string& name,
        const std::string& operator_text,
        const Value& value,
        std::size_t line) {

        auto assign_strings =
            [&](std::vector<std::string>& destination) {

                const std::vector<std::string> values =
                    strings(value, line);

                if (operator_text == "=") {
                    destination = values;
                } else {
                    destination.insert(
                        destination.end(),
                        values.begin(),
                        values.end());
                }
            };

        if (name == "sources") {
            assign_strings(target.sources);
            return;
        }

        if (name == "dependencies" ||
            name == "deps") {
            assign_strings(target.dependencies);
            return;
        }

        if (name == "generator_deps") {
            assign_strings(target.generator_deps);
            return;
        }

        if (name == "include_dirs") {
            assign_strings(target.include_dirs);
            return;
        }

        if (name == "cflags" ||
            name == "cxxflags") {

            assign_strings(target.cxxflags);

            target.flags["cxxflags"] =
                target.cxxflags;

            return;
        }

        if (name == "ldflags") {
            assign_strings(target.ldflags);
            return;
        }

        if (name == "toolchain_include") {
            assign_strings(target.toolchain_include);
            return;
        }

        if (name == "pkg_config") {
            assign_strings(target.pkg_config);
            return;
        }

        if (name == "library_dirs") {
            assign_strings(target.library_dirs);
            return;
        }

        if (name == "libraries") {
            assign_strings(target.libraries);
            return;
        }

        if (name == "frameworks") {
            assign_strings(target.frameworks);
            return;
        }

        if (name == "action") {
            assign_strings(target.action);
            return;
        }

        if (name == "inputs") {
            assign_strings(target.inputs);
            return;
        }

        if (name == "outputs") {
            assign_strings(target.outputs);
            return;
        }

        if (name == "output") {
            if (operator_text != "=") {
                error(
                    line,
                    "output does not support +=");
            }

            if (value.kind != ValueKind::String) {
                error(
                    line,
                    "output must be a string");
            }

            target.output =
                value.string;

            return;
        }

        if (name == "pre_actions") {
            if (operator_text == "=") {
                target.pre_actions =
                    action_lists(
                        value,
                        line);
            } else {
                const auto actions =
                    action_lists(
                        value,
                        line);

                target.pre_actions.insert(
                    target.pre_actions.end(),
                    actions.begin(),
                    actions.end());
            }

            return;
        }

        if (name == "flags") {
            error(
                line,
                "generic flags use flags.<name>");
        }

        if (name.rfind("flags.", 0) == 0) {
            const std::string flag_name =
                name.substr(6);

            if (!is_identifier(flag_name)) {
                error(
                    line,
                    "invalid generic flag name");
            }

            auto& destination =
                target.flags[flag_name];

            assign_strings(destination);

            if (flag_name == "cxxflags") {
                target.cxxflags =
                    destination;
            }

            return;
        }

        error(
            line,
            "unknown target property '" +
            name +
            "'");
    }

    std::string call_name(
        const std::string& text,
        std::size_t line) const {

        const std::size_t open =
            text.find('(');

        if (open == std::string::npos) {
            error(
                line,
                "expected declaration");
        }

        return trim(
            text.substr(
                0,
                open));
    }

    std::string call_argument(
        const std::string& text,
        std::size_t line) const {

        const std::size_t open =
            text.find('(');

        if (open == std::string::npos) {
            error(
                line,
                "invalid declaration");
        }

        const std::size_t close =
            text.rfind(')');

        if (close == std::string::npos ||
            close <= open) {
            error(
                line,
                "invalid declaration");
        }

        return trim(
            text.substr(
                open + 1,
                close - open - 1));
    }

    std::string declaration_type(
        const std::string& name,
        std::size_t line) const {

        if (name == "executable") {
            return "executable";
        }

        if (name == "app") {
            return "app";
        }

        if (name == "static_library" ||
            name == "static_lib") {
            return "static_lib";
        }

        if (name == "shared_library" ||
            name == "shared_lib") {
            return "shared_lib";
        }

        if (name == "object") {
            return "object";
        }

        if (name == "framework") {
            return "framework";
        }

        if (name == "generator") {
            return "generator";
        }

        if (name == "action") {
            return "ACTION";
        }

        error(
            line,
            "unknown declaration '" +
            name +
            "'");
    }

    Target& create_target(
        Project& project,
        const std::string& type,
        const std::string& name,
        std::size_t line) {

        if (project.targets.contains(name)) {
            error(
                line,
                "duplicate target '" +
                name +
                "'");
        }

        Target target;
        target.name = name;
        target.type = type;

        auto [it, inserted] =
            project.targets.emplace(
                name,
                std::move(target));

        (void)inserted;

        return it->second;
    }

    std::size_t block_end(
        std::size_t start,
        std::size_t end,
        std::size_t parent_indent) const {

        std::size_t result = start;

        while (result < end &&
               lines[result].indent > parent_indent) {
            ++result;
        }

        return result;
    }

    void parse_target_block(
        std::size_t start,
        std::size_t end,
        std::size_t indent,
        Target& target) {

        std::size_t i = start;

        while (i < end) {
            const SourceLine& line =
                lines[i];

            if (line.indent != indent) {
                error(
                    line.number,
                    "unexpected indentation");
            }

            const std::string text =
                trim(line.text);

            if (text.rfind("if ", 0) == 0 &&
                text.back() == ':') {

                const std::string expression =
                    trim(
                        text.substr(
                            3,
                            text.size() - 4));

                const std::size_t child_start =
                    i + 1;

                if (child_start >= end ||
                    lines[child_start].indent <= indent) {
                    error(
                        line.number,
                        "if requires a block");
                }

                const std::size_t child_end =
                    block_end(
                        child_start,
                        end,
                        indent);

                const bool active =
                    condition(
                        expression,
                        line.number);

                if (active) {
                    parse_target_block(
                        child_start,
                        child_end,
                        lines[child_start].indent,
                        target);
                }

                i = child_end;

                if (i < end &&
                    lines[i].indent == indent &&
                    trim(lines[i].text) == "else:") {

                    const std::size_t else_start =
                        i + 1;

                    if (else_start >= end ||
                        lines[else_start].indent <= indent) {
                        error(
                            lines[i].number,
                            "else requires a block");
                    }

                    const std::size_t else_end =
                        block_end(
                            else_start,
                            end,
                            indent);

                    if (!active) {
                        parse_target_block(
                            else_start,
                            else_end,
                            lines[else_start].indent,
                            target);
                    }

                    i = else_end;
                }

                continue;
            }

            if (text == "else:") {
                error(
                    line.number,
                    "unexpected else");
            }

            const auto [name, operator_text] =
                assignment(
                    text,
                    line.number);

            const std::string initial_value =
                assignment_value(
                    text,
                    operator_text,
                    line.number);

            const auto [complete_value, next] =
                collect_value(
                    i,
                    end,
                    initial_value,
                    line.number);

            const Value value =
                parse_value(
                    complete_value,
                    line.number);

            assign_target(
                target,
                name,
                operator_text,
                value,
                line.number);

            i = next;
        }
    }

    void parse_root_block(
        std::size_t start,
        std::size_t end,
        std::size_t indent,
        Project& project,
        ParsedManifest& manifest) {

        std::size_t i = start;

        while (i < end) {
            const SourceLine& line =
                lines[i];

            if (line.indent != indent) {
                error(
                    line.number,
                    "unexpected indentation");
            }

            const std::string text =
                trim(line.text);

            if (text.rfind("import ", 0) == 0) {
                const std::string argument =
                    trim(
                        text.substr(7));

                if (argument.empty()) {
                    error(
                        line.number,
                        "import requires a path");
                }

                if (argument.front() == '"') {
                    manifest.imports.push_back(
                        parse_string(
                            argument,
                            context(line.number)));
                } else {
                    manifest.imports.push_back(
                        argument);
                }

                ++i;
                continue;
            }

            if (text.rfind("declare_args(", 0) == 0 &&
                text.back() == ':') {
                const std::string declaration =
                    call_name(
                        text,
                        line.number);

                if (declaration != "declare_args") {
                    error(
                        line.number,
                        "invalid declare_args declaration");
                }

                const std::string name =
                    parse_string(
                        call_argument(
                            text,
                            line.number),
                        context(line.number));

                if (!is_identifier(name)) {
                    error(
                        line.number,
                        "invalid argument name '" +
                        name +
                        "'");
                }

                if (!discovering_args &&
                    parsed_args.contains(name)) {
                    error(
                        line.number,
                        "duplicate declared argument '" +
                        name +
                        "'");
                }

                if (!discovering_args) {
                    parsed_args.insert(name);
                }

                const std::size_t child_start = i + 1;

                if (child_start >= end ||
                    lines[child_start].indent <= indent) {
                    error(
                        line.number,
                        "declare_args requires a block");
                }

                const std::size_t child_end =
                    block_end(
                        child_start,
                        end,
                        indent);

                declared_args.insert(name);

                if (discovering_args) {
                    std::size_t child = child_start;

                    while (child < child_end) {
                        const SourceLine& child_line =
                            lines[child];

                        const auto [property, operator_text] =
                            assignment(
                                child_line.text,
                                child_line.number);

                        if (property != "default") {
                            error(
                                child_line.number,
                                "unknown declare_args property '" +
                                property +
                                "'");
                        }

                        if (operator_text != "=") {
                            error(
                                child_line.number,
                                "declare_args default does not support +=");
                        }

                        const std::string initial_value =
                            assignment_value(
                                child_line.text,
                                operator_text,
                                child_line.number);

                        const auto [complete_value, next] =
                            collect_value(
                                child,
                                child_end,
                                initial_value,
                                child_line.number);

                        variables[name] =
                            parse_value(
                                complete_value,
                                child_line.number);

                        child = next;
                    }

                    i = child_end;
                    continue;
                }

                std::size_t child = child_start;
                while (child < child_end) {
                    const SourceLine& child_line =
                        lines[child];

                    if (child_line.indent !=
                        lines[child_start].indent) {
                        error(
                            child_line.number,
                            "unexpected indentation");
                    }

                    const auto [property, operator_text] =
                        assignment(
                            child_line.text,
                            child_line.number);

                    if (property != "default") {
                        error(
                            child_line.number,
                            "unknown declare_args property '" +
                            property +
                            "'");
                    }

                    if (operator_text != "=") {
                        error(
                            child_line.number,
                            "declare_args default does not support +=");
                    }

                    const std::string initial_value =
                        assignment_value(
                            child_line.text,
                            operator_text,
                            child_line.number);

                    const auto [complete_value, next] =
                        collect_value(
                            child,
                            child_end,
                            initial_value,
                            child_line.number);

                    const Value value =
                        parse_value(
                            complete_value,
                            child_line.number);

                    if (!overridden_args.contains(name)) {
                    variables[name] = value;
                }
                    child = next;
                }

                i = child_end;
                continue;
            }

            if (text.rfind("if ", 0) == 0 &&
                text.back() == ':') {

                const std::string expression =
                    trim(
                        text.substr(
                            3,
                            text.size() - 4));

                const std::size_t child_start =
                    i + 1;

                if (child_start >= end ||
                    lines[child_start].indent <= indent) {
                    error(
                        line.number,
                        "if requires a block");
                }

                const std::size_t child_end =
                    block_end(
                        child_start,
                        end,
                        indent);

                const bool active =
                    condition(
                        expression,
                        line.number);

                if (active) {
                    parse_root_block(
                        child_start,
                        child_end,
                        lines[child_start].indent,
                        project,
                        manifest);
                }

                i = child_end;

                if (i < end &&
                    lines[i].indent == indent &&
                    trim(lines[i].text) == "else:") {

                    const std::size_t else_start =
                        i + 1;

                    if (else_start >= end ||
                        lines[else_start].indent <= indent) {
                        error(
                            lines[i].number,
                            "else requires a block");
                    }

                    const std::size_t else_end =
                        block_end(
                            else_start,
                            end,
                            indent);

                    if (!active) {
                        parse_root_block(
                            else_start,
                            else_end,
                            lines[else_start].indent,
                            project,
                            manifest);
                    }

                    i = else_end;
                }

                continue;
            }

            if (text == "else:") {
                error(
                    line.number,
                    "unexpected else");
            }

            if (text.rfind("project(", 0) == 0) {
                if (text.back() != ')') {
                    error(
                        line.number,
                        "project declaration must end with ')'");
                }

                const std::string name =
                    call_name(
                        text,
                        line.number);

                if (name != "project") {
                    error(
                        line.number,
                        "invalid project declaration");
                }

                const std::string argument =
                    call_argument(
                        text,
                        line.number);

                project.project =
                    parse_string(
                        argument,
                        context(line.number));

                ++i;
                continue;
            }

            if (text.rfind("default", 0) == 0) {
                const auto [name, operator_text] =
                    assignment(
                        text,
                        line.number);

                if (name != "default") {
                    error(
                        line.number,
                        "unknown project property '" +
                        name +
                        "'");
                }

                const std::string initial_value =
                    assignment_value(
                        text,
                        operator_text,
                        line.number);

                const auto [complete_value, next] =
                    collect_value(
                        i,
                        end,
                        initial_value,
                        line.number);

                const Value value =
                    parse_value(
                        complete_value,
                        line.number);

                const auto values =
                    strings(
                        value,
                        line.number);

                if (operator_text == "=") {
                    project.default_targets =
                        values;
                } else {
                    project.default_targets.insert(
                        project.default_targets.end(),
                        values.begin(),
                        values.end());
                }

                i = next;
                continue;
            }

            const std::size_t open =
                text.find('(');

            if (open != std::string::npos &&
                text.back() == ':') {

                const std::string declaration =
                    call_name(
                        text,
                        line.number);

                const std::string type =
                    declaration_type(
                        declaration,
                        line.number);

                const std::string name =
                    parse_string(
                        call_argument(
                            text,
                            line.number),
                        context(line.number));

                Target& target =
                    create_target(
                        project,
                        type,
                        name,
                        line.number);

                const std::size_t child_start =
                    i + 1;

                if (child_start >= end ||
                    lines[child_start].indent <= indent) {
                    error(
                        line.number,
                        "target declaration requires a block");
                }

                const std::size_t child_end =
                    block_end(
                        child_start,
                        end,
                        indent);

                parse_target_block(
                    child_start,
                    child_end,
                    lines[child_start].indent,
                    target);

                i = child_end;
                continue;
            }

            if (text.find('=') != std::string::npos) {
                const auto [name, operator_text] =
                    assignment(
                        text,
                        line.number);

                const std::string initial_value =
                    assignment_value(
                        text,
                        operator_text,
                        line.number);

                const auto [complete_value, next] =
                    collect_value(
                        i,
                        end,
                        initial_value,
                        line.number);

                const Value value =
                    parse_value(
                        complete_value,
                        line.number);

                assign_variable(
                    name,
                    operator_text,
                    value,
                    line.number);

                i = next;
                continue;
            }

            error(
                line.number,
                "unknown statement");
        }
    }

    ParsedManifest discover_args() {
        discovering_args = true;

        ParsedManifest result;

        if (!lines.empty()) {
            parse_root_block(
                0,
                lines.size(),
                lines.front().indent,
                result.project,
                result);
        }

        discovering_args = false;

        return result;
    }

    ParsedManifest parse() {
        ParsedManifest result;

        if (!lines.empty()) {
            parse_root_block(
                0,
                lines.size(),
                lines.front().indent,
                result.project,
                result);
        }

        for (auto& [name, target] :
             result.project.targets) {

            target.name = name;

            auto flags =
                target.flags.find("cxxflags");

            if (flags != target.flags.end()) {
                target.cxxflags =
                    flags->second;
            } else if (!target.cxxflags.empty()) {
                target.flags["cxxflags"] =
                    target.cxxflags;
            }
        }

        return result;
    }
};

fs::path canonical_manifest_path(
    const fs::path& path) {

    std::error_code ec;

    const fs::path absolute =
        fs::absolute(
            path,
            ec);

    if (ec) {
        throw std::runtime_error(
            "unable to resolve manifest path '" +
            path.string() +
            "': " +
            ec.message());
    }

    const fs::path canonical =
        fs::canonical(
            absolute,
            ec);

    if (ec) {
        throw std::runtime_error(
            "unable to resolve manifest '" +
            absolute.string() +
            "': " +
            ec.message());
    }

    return canonical;
}

ParsedManifest parse_single_file(
    const fs::path& path,
    ParserState& state,
    bool discover_args = false) {

    std::ifstream file(
        path,
        std::ios::binary);

    if (!file) {
        throw std::runtime_error(
            "unable to open NightBuild manifest: " +
            path.string());
    }

    std::ostringstream contents_stream;
    contents_stream << file.rdbuf();

    if (!file.good() &&
        !file.eof()) {
        throw std::runtime_error(
            "failed reading NightBuild manifest: " +
            path.string());
    }

    const std::string contents =
        contents_stream.str();

    Parser parser(
        path,
        contents,
        state);

    if (discover_args) {
        ParsedManifest result =
            parser.discover_args();

        result.canonical_path =
            path.string();

        result.raw_contents =
            contents;

        return result;
    }

    return parser.parse();
}

void apply_args_file(
    const fs::path& path,
    ParserState& state) {

    std::ifstream file(
        path,
        std::ios::binary);

    if (!file) {
        throw std::runtime_error(
            "unable to open NightBuild args file: " +
            path.string());
    }

    std::ostringstream contents_stream;
    contents_stream << file.rdbuf();

    if (!file.good() &&
        !file.eof()) {
        throw std::runtime_error(
            "failed reading NightBuild args file: " +
            path.string());
    }

    const std::string contents =
        contents_stream.str();

    Parser parser(
        path,
        contents,
        state);

    std::size_t i = 0;

    while (i < parser.lines.size()) {
        const SourceLine& line =
            parser.lines[i];

        const auto [name, operator_text] =
            parser.assignment(
                line.text,
                line.number);

        if (operator_text != "=") {
            parser.error(
                line.number,
                "args.nb only supports =");
        }

        if (!is_identifier(name)) {
            parser.error(
                line.number,
                "invalid argument name '" +
                name +
                "'");
        }

        if (!state.declared_args.contains(name)) {
            parser.error(
                line.number,
                "unknown argument '" +
                name +
                "'");
        }

        const std::string initial_value =
            parser.assignment_value(
                line.text,
                operator_text,
                line.number);

        const auto [complete_value, next] =
            parser.collect_value(
                i,
                parser.lines.size(),
                initial_value,
                line.number);

        state.variables[name] =
            parser.parse_value(
                complete_value,
                line.number);

        state.overridden_args.insert(name);

        i = next;
    }
}

struct IncludeLoader {
    ParserState state;
    std::unordered_set<std::string> loaded_files;
    std::unordered_set<std::string> active_files;
    std::vector<std::string> include_stack;

    std::vector<
        std::pair<std::string, std::string>>
        manifest_contents;

    Project load_root(
        const fs::path& root,
        const fs::path& build_dir) {

        const fs::path canonical =
            canonical_manifest_path(root);

        loaded_files.clear();
        active_files.clear();
        include_stack.clear();

        discover_file(canonical);

        const fs::path args_path =
            fs::absolute(build_dir) / "args.nb";

        if (fs::exists(args_path)) {
            apply_args_file(
                args_path,
                state);
        }

        loaded_files.clear();
        active_files.clear();
        include_stack.clear();
        manifest_contents.clear();

        ParsedManifest parsed =
            parse_single_file(
                canonical,
                state);

        Project result =
            std::move(parsed.project);

        const std::string canonical_string =
            canonical.string();

        loaded_files.insert(
            canonical_string);

        active_files.insert(
            canonical_string);

        include_stack.push_back(
            canonical_string);

        manifest_contents.emplace_back(
            canonical_string,
            parsed.raw_contents);

        for (const std::string& import :
             parsed.imports) {

            const fs::path child =
                canonical.parent_path() /
                import;

            load_child(
                child,
                result);
        }

        include_stack.pop_back();
        active_files.erase(
            canonical_string);

        return result;
    }

    void discover_file(
        const fs::path& path) {

        const fs::path canonical =
            canonical_manifest_path(path);

        const std::string canonical_string =
            canonical.string();

        if (active_files.contains(
                canonical_string)) {

            std::ostringstream error;

            error << "NightBuild import cycle detected:\n";

            auto it =
                std::find(
                    include_stack.begin(),
                    include_stack.end(),
                    canonical_string);

            if (it != include_stack.end()) {
                for (; it != include_stack.end(); ++it) {
                    error << "  "
                          << *it
                          << "\n";
                }
            }

            error << "  "
                  << canonical_string;

            throw std::runtime_error(
                error.str());
        }

        if (loaded_files.contains(
                canonical_string)) {
            return;
        }

        active_files.insert(
            canonical_string);

        include_stack.push_back(
            canonical_string);

        ParsedManifest parsed =
            parse_single_file(
                canonical,
                state,
                true);

        loaded_files.insert(
            canonical_string);

        for (const std::string& import :
             parsed.imports) {

            const fs::path child =
                canonical.parent_path() /
                import;

            discover_file(child);
        }

        include_stack.pop_back();
        active_files.erase(
            canonical_string);
    }

private:
    void load_child(
        const fs::path& path,
        Project& destination) {

        const fs::path canonical =
            canonical_manifest_path(path);

        const std::string canonical_string =
            canonical.string();

        if (active_files.contains(
                canonical_string)) {

            std::ostringstream error;

            error << "NightBuild import cycle detected:\n";

            auto it =
                std::find(
                    include_stack.begin(),
                    include_stack.end(),
                    canonical_string);

            if (it != include_stack.end()) {
                for (; it != include_stack.end(); ++it) {
                    error << "  "
                          << *it
                          << "\n";
                }
            }

            error << "  "
                  << canonical_string;

            throw std::runtime_error(
                error.str());
        }

        if (loaded_files.contains(
                canonical_string)) {
            return;
        }

        ParsedManifest parsed =
            parse_single_file(
                canonical,
                state);

        loaded_files.insert(
            canonical_string);

        active_files.insert(
            canonical_string);

        include_stack.push_back(
            canonical_string);

        manifest_contents.emplace_back(
            canonical_string,
            parsed.raw_contents);

        for (auto& [name, target] :
             parsed.project.targets) {

            if (destination.targets.contains(name)) {
                throw std::runtime_error(
                    "duplicate target '" +
                    name +
                    "' while loading '" +
                    canonical_string +
                    "'");
            }

            destination.targets.emplace(
                name,
                std::move(target));
        }

        for (const std::string& import :
             parsed.imports) {

            const fs::path child =
                canonical.parent_path() /
                import;

            load_child(
                child,
                destination);
        }

        include_stack.pop_back();
        active_files.erase(
            canonical_string);
    }
};

std::string sha256_bytes(
    const std::string& data) {

    unsigned char digest[
        CC_SHA256_DIGEST_LENGTH
    ];

    CC_SHA256(
        data.data(),
        static_cast<CC_LONG>(
            data.size()),
        digest);

    std::ostringstream result;

    for (unsigned char byte :
         digest) {

        result << std::hex
               << std::setw(2)
               << std::setfill('0')
               << static_cast<unsigned int>(
                      byte);
    }

    return result.str();
}

std::string configuration_hash(
    const IncludeLoader& loader) {

    std::string fingerprint;

    for (const auto& [path, contents] :
         loader.manifest_contents) {

        const std::uint64_t path_size =
            static_cast<std::uint64_t>(
                path.size());

        const std::uint64_t contents_size =
            static_cast<std::uint64_t>(
                contents.size());

        fingerprint.append(
            reinterpret_cast<const char*>(
                &path_size),
            sizeof(path_size));

        fingerprint.append(path);

        fingerprint.append(
            reinterpret_cast<const char*>(
                &contents_size),
            sizeof(contents_size));

        fingerprint.append(contents);
    }

    return sha256_bytes(
        fingerprint);
}

void validate_project(
    Project& project) {

    if (project.targets.empty()) {
        throw std::runtime_error(
            "NightBuild project contains no targets");
    }

    for (const std::string& name :
         project.default_targets) {

        if (!project.targets.contains(name)) {
            throw std::runtime_error(
                "project default target '" +
                name +
                "' does not exist");
        }
    }

    for (auto& [name, target] :
         project.targets) {

        target.name = name;

        if (target.type.empty()) {
            throw std::runtime_error(
                "target '" +
                name +
                "' is missing a type");
        }

        const bool valid_type =
            target.type == "executable" ||
            target.type == "app" ||
            target.type == "static_lib" ||
            target.type == "shared_lib" ||
            target.type == "object" ||
            target.type == "framework" ||
            target.type == "generator" ||
            target.type == "ACTION";

        if (!valid_type) {
            throw std::runtime_error(
                "target '" +
                name +
                "' has unknown type '" +
                target.type +
                "'");
        }

        const bool is_action =
            target.type == "ACTION";

        if (target.type == "generator") {
            if (target.sources.empty()) {
                throw std::runtime_error(
                    "generator target '" +
                    name +
                    "' requires sources");
            }

            if (!target.dependencies.empty()) {
                throw std::runtime_error(
                    "generator target '" +
                    name +
                    "' cannot use dependencies; "
                    "use generator_deps");
            }
        }

        if (is_action) {
            if (target.action.empty()) {
                throw std::runtime_error(
                    "ACTION target '" +
                    name +
                    "' requires action");
            }

            if (target.output.empty() &&
                target.outputs.empty()) {
                throw std::runtime_error(
                    "ACTION target '" +
                    name +
                    "' requires output or outputs");
            }

            if (target.output.empty() &&
                !target.outputs.empty()) {
                target.output =
                    target.outputs.front();
            }
        } else {
            if (target.sources.empty()) {
                throw std::runtime_error(
                    "target '" +
                    name +
                    "' requires at least one source");
            }

            if (target.type == "object" &&
                target.sources.size() != 1) {
                throw std::runtime_error(
                    "object target '" +
                    name +
                    "' requires exactly one source");
            }

            if (target.output.empty()) {
                if (target.type == "app") {
                    target.output =
                        name + ".app";
                } else if (
                    target.type == "static_lib") {
                    target.output =
                        "lib" + name + ".a";
                } else if (
                    target.type == "shared_lib") {
                    target.output =
                        "lib" + name + ".dylib";
                } else if (
                    target.type == "framework") {
                    target.output =
                        name + ".framework";
                } else if (
                    target.type == "object") {
                    target.output =
                        name + ".o";
                } else if (
                    target.type != "generator") {
                    target.output =
                        name;
                }
            }
        }

        for (const std::string& dependency :
             target.dependencies) {

            if (dependency == name) {
                throw std::runtime_error(
                    "target '" +
                    name +
                    "' cannot depend on itself");
            }

            if (!project.targets.contains(
                    dependency)) {
                throw std::runtime_error(
                    "target '" +
                    name +
                    "' depends on unknown target '" +
                    dependency +
                    "'");
            }

            if (project.targets.at(dependency).type ==
                "generator") {
                throw std::runtime_error(
                    "target '" +
                    name +
                    "' cannot use generator target '" +
                    dependency +
                    "' as a normal dependency; "
                    "use generator_deps");
            }
        }

        for (const std::string& dependency :
             target.generator_deps) {

            if (dependency == name) {
                throw std::runtime_error(
                    "target '" +
                    name +
                    "' cannot use itself as a "
                    "generator dependency");
            }

            if (!project.targets.contains(
                    dependency)) {
                throw std::runtime_error(
                    "target '" +
                    name +
                    "' has unknown generator dependency '" +
                    dependency +
                    "'");
            }

            if (project.targets.at(dependency).type !=
                "generator") {
                throw std::runtime_error(
                    "target '" +
                    name +
                    "' generator dependency '" +
                    dependency +
                    "' is not a generator target");
            }
        }

        auto flags =
            target.flags.find("cxxflags");

        if (flags != target.flags.end()) {
            target.cxxflags =
                flags->second;
        } else if (!target.cxxflags.empty()) {
            target.flags["cxxflags"] =
                target.cxxflags;
        }
    }
}

}  // namespace

Project parse_file(
    const std::string& path,
    const fs::path& build_dir) {

    const fs::path manifest(path);

    if (!fs::exists(manifest)) {
        throw std::runtime_error(
            "NightBuild manifest does not exist: " +
            manifest.string());
    }

    if (!fs::is_regular_file(manifest)) {
        throw std::runtime_error(
            "NightBuild manifest is not a regular file: " +
            manifest.string());
    }
    IncludeLoader loader;
    Project project =
        loader.load_root(
            manifest,
            build_dir);


    validate_project(project);

    project.configuration_hash =
        configuration_hash(loader);

    return project;
}

}  // namespace toml