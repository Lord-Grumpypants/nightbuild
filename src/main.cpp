#include "toml.hpp"
#include "tui.hpp"
#include "cli.hpp"
#include "scheduler.hpp"
#include "fsevents.hpp"
#include "command.hpp"
#include "compile_commands.hpp"

#include <algorithm>
#include <CommonCrypto/CommonDigest.h>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <spawn.h>
#include <pthread/qos.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace fs = std::filesystem;

namespace {

enum class OutputType : uint32_t {
    Executable,
    App,
    StaticLib,
    SharedLib,
    Object,
    Framework,
    Generator,
    Action
};

// enum class CommandKind : uint32_t {
//     PreAction,
//     Compile,
//     Object,
//     Link,
//     Generator,
//     Action
// };

// struct BuildPaths {
//     fs::path root;
//     fs::path build;
//     fs::path objects;
//     fs::path state;
//     fs::path commands;
// };

struct ProcessResult {
    int status = -1;
    std::string output;
};

struct PkgConfigFlags {
    std::vector<std::string> cflags;
    std::vector<std::string> libs;
};

struct Toolchain {
    std::string name;
    std::string extension;
    std::string compiler;
    std::string flags_key;
    std::vector<std::string> dependency_flags;
    std::string description;
    std::string output_extension;
};

// static constexpr char COMMAND_MAGIC[8] = {
//     'N', 'B', 'C', 'M', 'D', 'B', '\0', '\0'
// };

// static constexpr uint32_t COMMAND_VERSION = 2;

// #pragma pack(push, 1)

// struct CommandFileHeader {
//     char magic[8];

//     uint32_t version;
//     uint32_t flags;

//     uint64_t file_size;

//     uint64_t command_count;
//     uint64_t command_offset;

//     uint64_t argument_count;
//     uint64_t argument_offset;

//     uint64_t hash_count;
//     uint64_t hash_offset;

//     uint64_t string_offset;
//     uint64_t string_size;

//     uint64_t manifest_hash_offset;
// };


// struct HashRecord {
//     uint8_t digest[32];
// };

// #pragma pack(pop)

struct CommandDatabase {
    int fd = -1;
    size_t size = 0;
    void *mapping = MAP_FAILED;

    const CommandFileHeader *header = nullptr;
    const CommandRecord *commands = nullptr;
    const uint64_t *arguments = nullptr;
    const HashRecord *hashes = nullptr;
    const char *strings = nullptr;

    CommandDatabase() = default;

    CommandDatabase(const CommandDatabase &) = delete;
    CommandDatabase &operator=(const CommandDatabase &) = delete;

    CommandDatabase(CommandDatabase &&other) noexcept
        : fd(other.fd),
          size(other.size),
          mapping(other.mapping),
          header(other.header),
          commands(other.commands),
          arguments(other.arguments),
          hashes(other.hashes),
          strings(other.strings) {

        other.fd = -1;
        other.size = 0;
        other.mapping = MAP_FAILED;
        other.header = nullptr;
        other.commands = nullptr;
        other.arguments = nullptr;
        other.hashes = nullptr;
        other.strings = nullptr;
    }

    CommandDatabase &operator=(CommandDatabase &&other) noexcept {
        if (this == &other)
            return *this;

        close();

        fd = other.fd;
        size = other.size;
        mapping = other.mapping;
        header = other.header;
        commands = other.commands;
        arguments = other.arguments;
        hashes = other.hashes;
        strings = other.strings;

        other.fd = -1;
        other.size = 0;
        other.mapping = MAP_FAILED;
        other.header = nullptr;
        other.commands = nullptr;
        other.arguments = nullptr;
        other.hashes = nullptr;
        other.strings = nullptr;

        return *this;
    }

    ~CommandDatabase() {
        close();
    }

    void close() {
        if (mapping != MAP_FAILED) {
            munmap(mapping, size);
            mapping = MAP_FAILED;
        }

        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }

        size = 0;
        header = nullptr;
        commands = nullptr;
        arguments = nullptr;
        hashes = nullptr;
        strings = nullptr;
    }
};

// struct MutableCommandDatabase {
//     int fd = -1;
//     size_t size = 0;
//     void *mapping = MAP_FAILED;

//     CommandFileHeader *header = nullptr;
//     CommandRecord *commands = nullptr;
//     uint64_t *arguments = nullptr;
//     HashRecord *hashes = nullptr;
//     char *strings = nullptr;

//     MutableCommandDatabase() = default;

//     MutableCommandDatabase(const MutableCommandDatabase &) = delete;
//     MutableCommandDatabase &operator=(const MutableCommandDatabase &) = delete;

//     MutableCommandDatabase(MutableCommandDatabase &&other) noexcept
//         : fd(other.fd),
//           size(other.size),
//           mapping(other.mapping),
//           header(other.header),
//           commands(other.commands),
//           arguments(other.arguments),
//           hashes(other.hashes),
//           strings(other.strings) {

//         other.fd = -1;
//         other.size = 0;
//         other.mapping = MAP_FAILED;
//         other.header = nullptr;
//         other.commands = nullptr;
//         other.arguments = nullptr;
//         other.hashes = nullptr;
//         other.strings = nullptr;
//     }

//     MutableCommandDatabase &operator=(MutableCommandDatabase &&other) noexcept {
//         if (this == &other)
//             return *this;

//         close();

//         fd = other.fd;
//         size = other.size;
//         mapping = other.mapping;
//         header = other.header;
//         commands = other.commands;
//         arguments = other.arguments;
//         hashes = other.hashes;
//         strings = other.strings;

//         other.fd = -1;
//         other.size = 0;
//         other.mapping = MAP_FAILED;
//         other.header = nullptr;
//         other.commands = nullptr;
//         other.arguments = nullptr;
//         other.hashes = nullptr;
//         other.strings = nullptr;

//         return *this;
//     }

//     ~MutableCommandDatabase() {
//         close();
//     }

//     void close() {
//         if (mapping != MAP_FAILED) {
//             munmap(mapping, size);
//             mapping = MAP_FAILED;
//         }

//         if (fd >= 0) {
//             ::close(fd);
//             fd = -1;
//         }

//         size = 0;
//         header = nullptr;
//         commands = nullptr;
//         arguments = nullptr;
//         hashes = nullptr;
//         strings = nullptr;
//     }
// };

struct BuildRecord {
    CommandKind kind = CommandKind::Compile;
    std::string target;
    std::vector<std::string> argv;
    std::string description;
    std::string source;
    std::string output;
    std::string dependency;
    std::string source_hash;
    std::string dependency_hash;
    std::string input_hash;
    std::string state_hash;
};

[[noreturn]] void fail(const std::string &message) {
    throw std::runtime_error("NightBuild: " + message);
}

OutputType parse_output_type(const std::string &type) {
    if (type == "executable")
        return OutputType::Executable;

    if (type == "app")
        return OutputType::App;

    if (type == "static_lib")
        return OutputType::StaticLib;

    if (type == "shared_lib")
        return OutputType::SharedLib;

    if (type == "object")
        return OutputType::Object;

    if (type == "framework")
        return OutputType::Framework;

    if (type == "generator")
        return OutputType::Generator;

    if (type == "ACTION" || type == "action")
        return OutputType::Action;

    fail("unknown target type: " + type);
}

std::string command_kind_name(CommandKind kind) {
    switch (kind) {
    case CommandKind::PreAction:
        return "PRE";

    case CommandKind::Compile:
        return "CXX";

    case CommandKind::Object:
        return "OBJECT";

    case CommandKind::Link:
        return "LINK";

    case CommandKind::Generator:
        return "GEN";

    case CommandKind::Action:
        return "ACTION";
    }

    return "UNKNOWN";
}

std::string quote_for_display(const std::string &value) {
    if (value.find_first_of(" \t\\\"'") == std::string::npos)
        return value;

    return "\"" + value + "\"";
}

std::string join_command(
    const std::vector<std::string> &args) {

    std::ostringstream stream;

    for (size_t i = 0; i < args.size(); ++i) {
        if (i)
            stream << ' ';

        stream << quote_for_display(args[i]);
    }

    return stream.str();
}

std::vector<char *> make_argv(
    const std::vector<std::string> &args) {

    std::vector<char *> argv;
    argv.reserve(args.size() + 1);

    for (const auto &arg : args) {
        argv.push_back(
            const_cast<char *>(arg.c_str()));
    }

    argv.push_back(nullptr);
    return argv;
}

void ensure_parent_directory(const fs::path &path) {
    if (!path.has_parent_path())
        return;

    std::error_code ec;

    fs::create_directories(
        path.parent_path(),
        ec);

    if (ec) {
        fail(
            "cannot create output directory: " +
            path.parent_path().string() +
            ": " +
            ec.message());
    }
}

void prepare_command_outputs(
    const std::vector<std::string> &args) {

    for (size_t i = 0; i + 1 < args.size(); ++i) {
        const std::string &arg = args[i];

        if (arg == "-o" ||
            arg == "-MF" ||
            arg == "-MT") {

            ensure_parent_directory(
                fs::path(args[i + 1]));
        }
    }
}

int run_process(
    const std::vector<std::string> &args) {

    if (args.empty())
        fail("empty process command");

    prepare_command_outputs(args);

    std::vector<char *> argv =
        make_argv(args);

    pid_t pid = fork();

    if (pid < 0)
        fail("fork failed");

    if (pid == 0) {
        execvp(
            argv[0],
            argv.data());

        _exit(127);
    }

    int status = 0;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;

        fail("waitpid failed");
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);

    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);

    return 1;
}

ProcessResult run_capture_text(
    const std::vector<std::string> &args) {

    if (args.empty())
        fail("empty process command");

    int pipefd[2];

    if (pipe(pipefd) != 0)
        fail("pipe failed");

    std::vector<char *> argv =
        make_argv(args);

    pid_t pid = fork();

    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);

        fail("fork failed");
    }

    if (pid == 0) {
        close(pipefd[0]);

        if (dup2(
                pipefd[1],
                STDOUT_FILENO) < 0) {

            _exit(127);
        }

        if (dup2(
                pipefd[1],
                STDERR_FILENO) < 0) {

            _exit(127);
        }

        close(pipefd[1]);

        execvp(
            argv[0],
            argv.data());

        _exit(127);
    }

    close(pipefd[1]);

    std::string output;
    char buffer[4096];

    for (;;) {
        ssize_t count =
            read(
                pipefd[0],
                buffer,
                sizeof(buffer));

        if (count > 0) {
            output.append(
                buffer,
                static_cast<size_t>(count));

            continue;
        }

        if (count < 0 && errno == EINTR)
            continue;

        break;
    }

    close(pipefd[0]);

    int status = 0;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;

        fail("waitpid failed");
    }

    ProcessResult result;

    if (WIFEXITED(status)) {
        result.status =
            WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.status =
            128 + WTERMSIG(status);
    }

    result.output =
        std::move(output);

    return result;
}

std::string sha256_file(
    const fs::path &path) {

    std::ifstream file(
        path,
        std::ios::binary);

    if (!file)
        fail(
            "failed to open file for hashing: " +
            path.string());

    CC_SHA256_CTX context;

    if (CC_SHA256_Init(&context) != 1)
        fail(
            "failed to initialize SHA-256: " +
            path.string());

    std::array<char, 1024 * 1024> buffer;

    while (file) {
        file.read(
            buffer.data(),
            buffer.size());

        std::streamsize count =
            file.gcount();

        if (count > 0) {
            if (CC_SHA256_Update(
                    &context,
                    buffer.data(),
                    static_cast<size_t>(count)) != 1) {
                fail(
                    "failed to hash file: " +
                    path.string());
            }
        }
    }

    if (!file.eof())
        fail(
            "failed to read file for hashing: " +
            path.string());

    unsigned char digest[CC_SHA256_DIGEST_LENGTH];

    if (CC_SHA256_Final(
            digest,
            &context) != 1) {
        fail(
            "failed to finalize SHA-256: " +
            path.string());
    }

    static constexpr char hex[] =
        "0123456789abcdef";

    std::string hash;
    hash.reserve(
        CC_SHA256_DIGEST_LENGTH * 2);

    for (unsigned char byte : digest) {
        hash.push_back(
            hex[byte >> 4]);
        hash.push_back(
            hex[byte & 0x0f]);
    }

    return hash;
}


std::string sha256_text(
    const std::string &text) {

    char temp_name[] =
        "/tmp/nightbuild-hash-XXXXXX";

    int fd =
        mkstemp(temp_name);

    if (fd < 0)
        fail("mkstemp failed");

    size_t total = 0;

    while (total < text.size()) {
        ssize_t written =
            write(
                fd,
                text.data() + total,
                text.size() - total);

        if (written < 0) {
            if (errno == EINTR)
                continue;

            close(fd);
            unlink(temp_name);

            fail(
                "failed to write temporary hash file");
        }

        if (written == 0) {
            close(fd);
            unlink(temp_name);

            fail(
                "failed to make progress writing temporary hash file");
        }

        total +=
            static_cast<size_t>(written);
    }

    close(fd);

    std::string hash =
        sha256_file(temp_name);

    unlink(temp_name);

    return hash;
}

std::string read_file(
    const fs::path &path) {

    std::ifstream file(
        path,
        std::ios::binary);

    if (!file)
        fail(
            "cannot read file: " +
            path.string());

    std::ostringstream stream;
    stream << file.rdbuf();

    return stream.str();
}

void write_file(
    const fs::path &path,
    const std::string &content) {

    if (path.has_parent_path()) {
        std::error_code ec;

        fs::create_directories(
            path.parent_path(),
            ec);

        if (ec)
            fail(
                "cannot create output directory: " +
                path.parent_path().string() +
                ": " +
                ec.message());
    }

    std::ofstream file(
        path,
        std::ios::binary);

    if (!file)
        fail(
            "cannot write file: " +
            path.string());

    file << content;

    if (!file)
        fail(
            "failed writing file: " +
            path.string());
}

BuildPaths make_paths(
    const fs::path &build_dir) {

    BuildPaths paths;

    paths.build =
        fs::absolute(build_dir);

    paths.root = fs::current_path();

    paths.objects =
        paths.build / "obj";


    paths.commands =
        paths.build / "commands";
    


    return paths;
}

fs::path source_path(
    const BuildPaths &paths,
    const std::string &source) {

    fs::path value(source);

    if (value.is_absolute())
        return value.lexically_normal();

    return (
        paths.root /
        value
    ).lexically_normal();
}

fs::path object_path(
    const BuildPaths &paths,
    const std::string &source) {

    fs::path relative(source);

    if (relative.is_absolute()) {
        relative =
            relative.lexically_relative(
                paths.root);
    }

    fs::path result =
        paths.objects / relative;

    result.replace_extension(".o");

    return result.lexically_normal();
}

fs::path dependency_path(
    const BuildPaths &paths,
    const std::string &source) {

    fs::path relative(source);

    if (relative.is_absolute()) {
        relative =
            relative.lexically_relative(
                paths.root);
    }

    fs::path result =
        paths.objects / relative;

    result.replace_extension(".d");

    return result.lexically_normal();
}



bool file_exists(
    const fs::path &path) {

    std::error_code ec;

    return fs::exists(
        path,
        ec);
}

std::vector<std::string> split_words(
    const std::string &text) {

    std::vector<std::string> result;

    std::istringstream stream(text);

    std::string word;

    while (stream >> word)
        result.push_back(word);

    return result;
}

std::vector<std::string> pkg_config_flags(
     const toml::Target &target,
    const char *mode) {

    std::vector<std::string> result;

    for (const auto &package :
         target.pkg_config) {

        ProcessResult pkg =
            run_capture_text({
                "pkg-config",
                mode,
                package
            });

        if (pkg.status != 0) {
            fail(
                std::string("pkg-config ") +
                mode +
                " failed for " +
                package +
                "\n" +
                pkg.output);
        }

        auto words =
            split_words(pkg.output);

        result.insert(
            result.end(),
            words.begin(),
            words.end());
    }

    return result;
}

PkgConfigFlags resolve_pkg_config(
    const toml::Target &target) {

    PkgConfigFlags flags;

    if (target.pkg_config.empty())
        return flags;

    flags.cflags =
        pkg_config_flags(
            target,
            "--cflags");

    flags.libs =
        pkg_config_flags(
            target,
            "--libs");

    return flags;
}

/*
 * --------------------------------------------------------------------------
 * JSON TOOLCHAIN LOADER
 * --------------------------------------------------------------------------
 *
 * NightBuild intentionally uses a small JSON parser for toolchain metadata.
 *
 * Supported JSON values:
 *
 *   "key": "string"
 *   "key": ["string", "string"]
 *
 * Unlike the old parser, strings and arrays are represented by distinct
 * types. This prevents a one-element array from being silently accepted
 * where a string was required.
 */

struct JsonValue {
    enum class Type {
        String,
        Array
    };

    Type type = Type::String;
    std::string string_value;
    std::vector<std::string> array_value;

    static JsonValue string(
        std::string value) {

        JsonValue result;

        result.type = Type::String;
        result.string_value =
            std::move(value);

        return result;
    }

    static JsonValue array(
        std::vector<std::string> value) {

        JsonValue result;

        result.type = Type::Array;
        result.array_value =
            std::move(value);

        return result;
    }
};

class JsonParser {
public:
    explicit JsonParser(
        const std::string &text)
        : text_(text) {

        /*
         * UTF-8 BOM:
         *
         * EF BB BF
         *
         * It is legal for some editors to emit this at the beginning
         * of a UTF-8 JSON file. Treat it as metadata rather than JSON.
         */
        if (text_.size() >= 3 &&
            static_cast<unsigned char>(text_[0]) == 0xEF &&
            static_cast<unsigned char>(text_[1]) == 0xBB &&
            static_cast<unsigned char>(text_[2]) == 0xBF) {

            position_ = 3;
        }
    }

    std::unordered_map<std::string, JsonValue>
    parse_object() {

        skip_whitespace();

        expect('{');

        std::unordered_map<
            std::string,
            JsonValue> result;

        skip_whitespace();

        if (consume('}'))
            return result;

        for (;;) {
            skip_whitespace();

            std::string key =
                parse_string();

            skip_whitespace();
            expect(':');
            skip_whitespace();

            if (peek() == '"') {
                result[key] =
                    JsonValue::string(
                        parse_string());

            } else if (peek() == '[') {
                result[key] =
                    JsonValue::array(
                        parse_string_array());

            } else {
                error(
                    "expected JSON string or array for '" +
                    key +
                    "'");
            }

            skip_whitespace();

            if (consume('}'))
                break;

            expect(',');
        }

        skip_whitespace();

        if (position_ != text_.size())
            error(
                "unexpected data after JSON object");

        return result;
    }

private:
    const std::string &text_;
    size_t position_ = 0;

    char peek() const {
        if (position_ >= text_.size())
            return '\0';

        return text_[position_];
    }

    void skip_whitespace() {
        while (position_ < text_.size() &&
               std::isspace(
                   static_cast<unsigned char>(
                       text_[position_]))) {

            ++position_;
        }
    }

    void expect(char expected) {
        if (peek() != expected) {
            error(
                std::string("expected '") +
                expected +
                "'");
        }

        ++position_;
    }

    bool consume(char value) {
        if (peek() != value)
            return false;

        ++position_;
        return true;
    }

    [[noreturn]] void error(
        const std::string &message) const {

        throw std::runtime_error(
            "invalid toolchain JSON at byte " +
            std::to_string(position_) +
            ": " +
            message);
    }

    std::string parse_string() {
        if (peek() != '"')
            error("expected string");

        ++position_;

        std::string result;

        while (position_ < text_.size()) {
            char c =
                text_[position_++];

            if (c == '"')
                return result;

            if (static_cast<unsigned char>(c) < 0x20) {
                error(
                    "unescaped control character in JSON string");
            }

            if (c != '\\') {
                result += c;
                continue;
            }

            if (position_ >= text_.size())
                error(
                    "unterminated escape sequence");

            char escaped =
                text_[position_++];

            switch (escaped) {
            case '"':
                result += '"';
                break;

            case '\\':
                result += '\\';
                break;

            case '/':
                result += '/';
                break;

            case 'b':
                result += '\b';
                break;

            case 'f':
                result += '\f';
                break;

            case 'n':
                result += '\n';
                break;

            case 'r':
                result += '\r';
                break;

            case 't':
                result += '\t';
                break;

            case 'u':
                error(
                    "Unicode JSON escapes are not supported "
                    "in toolchain metadata");

            default:
                error(
                    "unsupported JSON escape sequence");
            }
        }

        error(
            "unterminated JSON string");
    }

    std::vector<std::string> parse_string_array() {
        expect('[');

        std::vector<std::string> result;

        skip_whitespace();

        if (consume(']'))
            return result;

        for (;;) {
            skip_whitespace();

            if (peek() != '"') {
                error(
                    "JSON arrays in toolchain metadata "
                    "must contain only strings");
            }

            result.push_back(
                parse_string());

            skip_whitespace();

            if (consume(']'))
                break;

            expect(',');
        }

        return result;
    }
};

std::string json_string(
    const std::unordered_map<
        std::string,
        JsonValue> &values,
    const std::string &key,
    const fs::path &path) {

    auto it =
        values.find(key);

    if (it == values.end())
        fail(
            "toolchain JSON is missing '" +
            key +
            "': " +
            path.string());

    if (it->second.type !=
        JsonValue::Type::String) {

        fail(
            "toolchain JSON field '" +
            key +
            "' must be a string: " +
            path.string());
    }

    return it->second.string_value;
}

std::vector<std::string> json_array(
    const std::unordered_map<
        std::string,
        JsonValue> &values,
    const std::string &key,
    const fs::path &path) {

    auto it =
        values.find(key);

    if (it == values.end())
        return {};

    if (it->second.type !=
        JsonValue::Type::Array) {

        fail(
            "toolchain JSON field '" +
            key +
            "' must be an array of strings: " +
            path.string());
    }

    return it->second.array_value;
}

std::string normalize_extension(
    std::string extension) {

    if (extension.empty())
        fail(
            "toolchain extension cannot be empty");

    if (extension.front() != '.')
        extension.insert(
            extension.begin(),
            '.');

    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char c) {
            return static_cast<char>(
                std::tolower(c));
        });

    return extension;
}

Toolchain load_toolchain(
    const fs::path &path) {

    if (!file_exists(path))
        fail(
            "toolchain file not found: " +
            path.string());

    std::string content =
        read_file(path);

    if (content.empty())
        fail(
            "toolchain JSON is empty: " +
            path.string());

    JsonParser parser(content);

    auto values =
        parser.parse_object();

    Toolchain toolchain;

    toolchain.name =
        json_string(
            values,
            "name",
            path);

    toolchain.extension =
        normalize_extension(
            json_string(
                values,
                "extension",
                path));

    toolchain.compiler =
        json_string(
            values,
            "compiler",
            path);

    toolchain.flags_key =
        json_string(
            values,
            "flags_key",
            path);

    /*
     * Preferred form:
     *
     *   "dependency_flags": [
     *       "-MMD",
     *       "-MF"
     *   ]
     *
     * Compatibility form:
     *
     *   "dep_flag": "-MMD -MF"
     */
    if (values.find("dependency_flags") !=
        values.end()) {

        toolchain.dependency_flags =
            json_array(
                values,
                "dependency_flags",
                path);

    } else {

        auto it =
            values.find("dep_flag");

        if (it != values.end()) {

            if (it->second.type !=
                JsonValue::Type::String) {

                fail(
                    "toolchain JSON field 'dep_flag' "
                    "must be a string: " +
                    path.string());
            }

            toolchain.dependency_flags =
                split_words(
                    it->second.string_value);
        }
    }

    toolchain.description =
        json_string(
            values,
            "description",
            path);

    toolchain.output_extension =
        json_string(
            values,
            "output_extension",
            path);

    if (toolchain.compiler.empty())
        fail(
            "toolchain compiler cannot be empty: " +
            path.string());

    if (toolchain.flags_key.empty())
        fail(
            "toolchain flags_key cannot be empty: " +
            path.string());

    if (toolchain.output_extension.empty())
        fail(
            "toolchain output_extension cannot be empty: " +
            path.string());

    if (toolchain.output_extension.front() != '.')
        toolchain.output_extension.insert(
            toolchain.output_extension.begin(),
            '.');

    return toolchain;
}

std::vector<Toolchain> load_toolchains(
    const BuildPaths &paths,
    const toml::Target &target) {

    std::vector<Toolchain> toolchains;

    const std::vector<std::string> c_extensions = {
        ".c"
    };

    for (const auto &extension :
         c_extensions) {

        Toolchain toolchain;

        toolchain.name =
            "C";

        toolchain.extension =
            extension;

        toolchain.compiler =
            "clang";

        toolchain.flags_key =
            "cflags";

        toolchain.dependency_flags = {
            "-MMD",
            "-MP"
        };

        toolchain.description =
            "C";

        toolchain.output_extension =
            ".o";

        toolchains.push_back(
            std::move(toolchain));
    }

    const std::vector<std::string> cxx_extensions = {
        ".cc",
        ".cp",
        ".cpp",
        ".cxx",
        ".c++"
    };

    for (const auto &extension :
         cxx_extensions) {

        Toolchain toolchain;

        toolchain.name =
            "C/C++";

        toolchain.extension =
            extension;

        toolchain.compiler =
            "clang++";

        toolchain.flags_key =
            "cxxflags";

        toolchain.dependency_flags = {
            "-MMD",
            "-MP"
        };

        toolchain.description =
            "CXX";

        toolchain.output_extension =
            ".o";

        toolchains.push_back(
            std::move(toolchain));
    }

    {
        Toolchain toolchain;

        toolchain.name =
            "Objective-C";

        toolchain.extension =
            ".m";

        toolchain.compiler =
            "clang";

        toolchain.flags_key =
            "objcflags";

        toolchain.dependency_flags = {
            "-MMD",
            "-MP"
        };

        toolchain.description =
            "OBJC";

        toolchain.output_extension =
            ".o";

        toolchains.push_back(
            std::move(toolchain));
    }

    {
        Toolchain toolchain;

        toolchain.name =
            "Objective-C++";

        toolchain.extension =
            ".mm";

        toolchain.compiler =
            "clang++";

        toolchain.flags_key =
            "objcxxflags";

        toolchain.dependency_flags = {
            "-MMD",
            "-MP"
        };

        toolchain.description =
            "OBJCXX";

        toolchain.output_extension =
            ".o";

        toolchains.push_back(
            std::move(toolchain));
    }

    {
        Toolchain toolchain;

        toolchain.name =
            "Swift";

        toolchain.extension =
            ".swift";

        toolchain.compiler =
            "swiftc";

        toolchain.flags_key =
            "swiftflags";

        toolchain.description =
            "SWIFT";

        toolchain.output_extension =
            ".o";

        toolchains.push_back(
            std::move(toolchain));
    }

    for (const auto &include :
         target.toolchain_include) {

        if (include.empty())
            fail(
                "toolchain_include contains an empty path");

        fs::path path(include);

        if (path.is_relative())
            path =
                paths.root /
                path;

        path =
            fs::absolute(
                path).lexically_normal();

        Toolchain toolchain =
            load_toolchain(path);

        bool replaced = false;

        for (auto &existing :
             toolchains) {

            if (existing.extension ==
                toolchain.extension) {

                existing =
                    std::move(toolchain);

                replaced = true;
                break;
            }
        }

        if (!replaced)
            toolchains.push_back(
                std::move(toolchain));
    }

    return toolchains;
}

const Toolchain *find_toolchain(
    const std::vector<Toolchain> &toolchains,
    const std::string &source) {

    fs::path path(source);

    std::string extension =
        path.extension().string();

    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char c) {
            return static_cast<char>(
                std::tolower(c));
        });

    for (const auto &toolchain :
         toolchains) {

        if (toolchain.extension ==
            extension) {

            return &toolchain;
        }
    }

    return nullptr;
}

const std::vector<std::string> *flags_for_key(
    const toml::Target &target,
    const std::string &key) {

    auto it =
        target.flags.find(key);

    if (it != target.flags.end())
        return &it->second;

    if (key == "cxxflags")
        return &target.cxxflags;

    return nullptr;
}

std::vector<std::string> toolchain_flags(
    const toml::Target &target,
    const PkgConfigFlags &pkg,
    const Toolchain &toolchain) {

    std::vector<std::string> flags;

    for (const auto &dir :
         target.include_dirs) {

        flags.push_back("-I");
        flags.push_back(dir);
    }

    const auto *custom_flags =
        flags_for_key(
            target,
            toolchain.flags_key);

    if (custom_flags != nullptr) {

        flags.insert(
            flags.end(),
            custom_flags->begin(),
            custom_flags->end());

    } else if (toolchain.flags_key ==
               "cxxflags") {

        flags.insert(
            flags.end(),
            target.cxxflags.begin(),
            target.cxxflags.end());

    } else {

        fail(
            "toolchain '" +
            toolchain.name +
            "' requires flags key '" +
            toolchain.flags_key +
            "', but it is not defined");
    }

    flags.insert(
        flags.end(),
        pkg.cflags.begin(),
        pkg.cflags.end());

    return flags;
}

std::vector<std::string> compiler_flags(
    const toml::Target &target,
    const PkgConfigFlags &pkg) {

    std::vector<std::string> flags;

    for (const auto &dir :
         target.include_dirs) {

        flags.push_back("-I");
        flags.push_back(dir);
    }

    flags.insert(
        flags.end(),
        target.cxxflags.begin(),
        target.cxxflags.end());

    flags.insert(
        flags.end(),
        pkg.cflags.begin(),
        pkg.cflags.end());

    return flags;
}

std::vector<std::string> link_flags(
    const toml::Target &target,
    const PkgConfigFlags &pkg) {
    std::vector<std::string> flags;

    for (const auto &dir :
         target.library_dirs) {
        flags.push_back("-L");
        flags.push_back(dir);
    }

    flags.insert(
        flags.end(),
        target.ldflags.begin(),
        target.ldflags.end());

    for (const auto &library :
         target.libraries) {
        flags.push_back("-l");
        flags.push_back(library);
    }

    for (const auto &framework :
         target.frameworks) {
        flags.push_back("-framework");
        flags.push_back(framework);
    }

    flags.insert(
        flags.end(),
        pkg.libs.begin(),
        pkg.libs.end());

    return flags;
}

fs::path toolchain_output_path(
    const BuildPaths &paths,
    const std::string &source,
    const Toolchain &toolchain) {

    fs::path relative(source);

    if (relative.is_absolute()) {
        relative =
            relative.lexically_relative(
                paths.root);
    }

    fs::path result =
        paths.objects / relative;

    result.replace_extension(
        toolchain.output_extension);

    return result.lexically_normal();
}

fs::path toolchain_dependency_path(
    const BuildPaths &paths,
    const std::string &source) {

    fs::path relative(source);

    if (relative.is_absolute()) {
        relative =
            relative.lexically_relative(
                paths.root);
    }

    fs::path result =
        paths.objects / relative;

    result.replace_extension(".d");

    return result.lexically_normal();
}

std::vector<std::string> compile_command(
    const BuildPaths &paths,
    const toml::Target &target,
    const std::string &source,
    const PkgConfigFlags &pkg,
    const std::vector<Toolchain> &toolchains) {

    const Toolchain *toolchain =
        find_toolchain(
            toolchains,
            source);

    if (toolchain == nullptr)
        fail(
            "no toolchain registered for source '" +
            source +
            "'");

    fs::path source_file =
        source_path(
            paths,
            source);

    fs::path object =
        toolchain_output_path(
            paths,
            source,
            *toolchain);

    fs::path dependency =
        toolchain_dependency_path(
            paths,
            source);

    std::vector<std::string> command = {
        toolchain->compiler
    };

    auto flags =
        toolchain_flags(
            target,
            pkg,
            *toolchain);

    command.insert(
        command.end(),
        flags.begin(),
        flags.end());

    bool has_mf = false;

    for (const auto &dependency_flag :
         toolchain->dependency_flags) {

        command.push_back(
            dependency_flag);

        if (dependency_flag == "-MF") {
            command.push_back(
                dependency.string());

            has_mf = true;
        }
    }

    if (!has_mf) {
        command.push_back("-MF");
        command.push_back(
            dependency.string());
    }

    command.push_back("-MT");
    command.push_back(
        object.string());

    command.push_back("-c");

    command.push_back(
        source_file.string());

    command.push_back("-o");

    command.push_back(
        object.string());

    return command;
}

std::vector<fs::path> target_objects(
    const BuildPaths &paths,
    const toml::Target &target,
    const std::vector<Toolchain> &toolchains) {

    std::vector<fs::path> objects;

    for (const auto &source :
         target.sources) {

        const Toolchain *toolchain =
            find_toolchain(
                toolchains,
                source);

        if (toolchain == nullptr)
            fail(
                "no toolchain registered for source '" +
                source +
                "'");

        objects.push_back(
            toolchain_output_path(
                paths,
                source,
                *toolchain));
    }

    return objects;
}

std::vector<std::string> executable_link_command(
    const BuildPaths &paths,
    const toml::Target &target,
    const std::vector<fs::path> &link_inputs,
    const PkgConfigFlags &pkg) {

    std::vector<std::string> command = {
        "clang++"
    };

    for (const auto &object :
         link_inputs) {

        command.push_back(
            object.string());
    }

    command.push_back("-o");

    command.push_back(
        (
            paths.root /
            target.output
        ).string());

    auto flags =
        link_flags(
            target,
            pkg);

    command.insert(
        command.end(),
        flags.begin(),
        flags.end());

    return command;
}

std::vector<std::string> generator_link_command(
    const BuildPaths &paths,
    const toml::Target &target,
    const std::vector<fs::path> &link_inputs,
    const PkgConfigFlags &pkg) {
    toml::Target generator = target;
    generator.output =
        (paths.build /
         "generators" /
         target.name).string();

    return executable_link_command(
        paths,
        generator,
        link_inputs,
        pkg);
}

std::vector<std::string> static_library_command(
    const BuildPaths &paths,
    const toml::Target &target,
    const std::vector<fs::path> &link_inputs) {

    std::vector<std::string> command = {
        "ar",
        "rcs",
        (
            paths.root /
            target.output
        ).string()
    };

    for (const auto &object :
         link_inputs) {

        command.push_back(
            object.string());
    }

    return command;
}

std::vector<std::string> shared_library_command(
    const BuildPaths &paths,
    const toml::Target &target,
    const std::vector<fs::path> &link_inputs,
    const PkgConfigFlags &pkg) {

    std::vector<std::string> command = {
        "clang++",
        "-dynamiclib"
    };

    for (const auto &object :
         link_inputs) {

        command.push_back(
            object.string());
    }

    command.push_back("-o");

    command.push_back(
        (
            paths.root /
            target.output
        ).string());

    auto flags =
        link_flags(
            target,
            pkg);

    command.insert(
        command.end(),
        flags.begin(),
        flags.end());

    return command;
}

fs::path app_bundle_path(
    const BuildPaths &paths,
    const toml::Target &target) {

    return (
        paths.root /
        target.output
    ).lexically_normal();
}

fs::path app_contents_path(
    const BuildPaths &paths,
    const toml::Target &target) {

    return (
        app_bundle_path(
            paths,
            target) /
        "Contents"
    ).lexically_normal();
}

fs::path app_executable_path(
    const BuildPaths &paths,
    const toml::Target &target) {

    return (
        app_contents_path(
            paths,
            target) /
        "MacOS" /
        target.name
    ).lexically_normal();
}

fs::path app_info_plist_path(
    const BuildPaths &paths,
    const toml::Target &target) {

    return (
        app_contents_path(
            paths,
            target) /
        "Info.plist"
    ).lexically_normal();
}

std::string make_app_info_plist(
    const std::string &name) {

    std::ostringstream plist;

    plist
        << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<!DOCTYPE plist PUBLIC "
        << "\"-//Apple//DTD PLIST 1.0//EN\" "
        << "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        << "<plist version=\"1.0\">\n"
        << "<dict>\n"
        << "    <key>CFBundleDisplayName</key>\n"
        << "    <string>"
        << name
        << "</string>\n"
        << "    <key>CFBundleExecutable</key>\n"
        << "    <string>"
        << name
        << "</string>\n"
        << "    <key>CFBundleIdentifier</key>\n"
        << "    <string>com.nightfall."
        << name
        << "</string>\n"
        << "    <key>CFBundleName</key>\n"
        << "    <string>"
        << name
        << "</string>\n"
        << "    <key>CFBundlePackageType</key>\n"
        << "    <string>APPL</string>\n"
        << "    <key>CFBundleVersion</key>\n"
        << "    <string>1</string>\n"
        << "    <key>CFBundleShortVersionString</key>\n"
        << "    <string>1.0</string>\n"
        << "</dict>\n"
        << "</plist>\n";

    return plist.str();
}

void prepare_app_bundle(
    const BuildPaths &paths,
    const toml::Target &target) {

    fs::create_directories(
        app_contents_path(
            paths,
            target) /
        "MacOS");

    write_file(
        app_info_plist_path(
            paths,
            target),
        make_app_info_plist(target.name));
}

std::vector<std::string> app_link_command(
    const BuildPaths &paths,
    const toml::Target &target,
    const std::vector<fs::path> &link_inputs,
    const PkgConfigFlags &pkg) {

    std::vector<std::string> command = {
        "clang++"
    };

    for (const auto &object :
         link_inputs) {

        command.push_back(
            object.string());
    }

    command.push_back("-o");

    command.push_back(
        app_executable_path(
            paths,
            target).string());

    auto flags =
        link_flags(
            target,
            pkg);

    command.insert(
        command.end(),
        flags.begin(),
        flags.end());

    return command;
}

std::vector<std::string> framework_command(
    const BuildPaths &paths,
    const toml::Target &target,
    const std::vector<fs::path> &link_inputs,
    const PkgConfigFlags &pkg) {

    fs::path framework =
        paths.root /
        target.output;

    fs::path executable =
        framework /
        target.name;

    std::vector<std::string> command = {
        "clang++",
        "-dynamiclib"
    };

    for (const auto &object :
         link_inputs) {

        command.push_back(
            object.string());
    }

    command.push_back("-o");

    command.push_back(
        executable.string());

    auto flags =
        link_flags(
            target,
            pkg);

    command.insert(
        command.end(),
        flags.begin(),
        flags.end());

    return command;
}

std::vector<std::string> object_command(
    const BuildPaths &paths,
    const toml::Target &target,
    const PkgConfigFlags &pkg,
    const std::vector<Toolchain> &toolchains) {

    if (target.sources.size() != 1)
        fail(
            "object target requires exactly one source");

    const std::string &source =
        target.sources.front();

    const Toolchain *toolchain =
        find_toolchain(
            toolchains,
            source);

    if (toolchain == nullptr)
        fail(
            "no toolchain registered for source '" +
            source +
            "'");

    std::vector<std::string> command = {
        toolchain->compiler
    };

    auto flags =
        toolchain_flags(
            target,
            pkg,
            *toolchain);

    command.insert(
        command.end(),
        flags.begin(),
        flags.end());

    command.push_back("-c");

    command.push_back(
        source_path(
            paths,
            source).string());

    command.push_back("-o");

    command.push_back(
        (
            paths.root /
            target.output
        ).string());

    return command;
}

std::string normalize_hash(
    const std::string &hash) {

    std::string result = hash;

    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](unsigned char c) {
            return static_cast<char>(
                std::tolower(c));
        });

    return result;
}

std::array<uint8_t, 32> decode_hash(
    const std::string &text) {

    std::array<uint8_t, 32> result{};

    std::string value =
        normalize_hash(text);

    if (value.size() != 64)
        fail("invalid SHA-256 length");

    auto hex =
        [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';

            if (c >= 'a' && c <= 'f')
                return 10 + c - 'a';

            return -1;
        };

    for (size_t i = 0; i < 32; ++i) {
        int high = hex(value[i * 2]);
        int low = hex(value[i * 2 + 1]);

        if (high < 0 || low < 0)
            fail("invalid SHA-256");

        result[i] =
            static_cast<uint8_t>(
                (high << 4) | low);
    }

    return result;
}

std::string cached_sha256_file(
    MutableCommandDatabase &database,
    uint64_t hash_index,
    const fs::path &path) {

    if (hash_index >=
        database.header->hash_count) {
        fail("invalid hash index");
    }

    struct stat st{};

    if (stat(
            path.c_str(),
            &st) != 0) {
        fail(
            "failed to stat file: " +
            path.string());
    }

    HashRecord &record =
        database.hashes[hash_index];

    std::int64_t seconds =
        static_cast<std::int64_t>(
            st.st_mtimespec.tv_sec);

    std::int32_t nanoseconds =
        static_cast<std::int32_t>(
            st.st_mtimespec.tv_nsec);

    static constexpr char hex[] =
        "0123456789abcdef";

    if (record.mtime_seconds == seconds &&
        record.mtime_nanoseconds == nanoseconds) {

        std::string result;
        result.reserve(64);

        for (std::uint8_t byte :
             record.digest) {
            result.push_back(
                hex[byte >> 4]);
            result.push_back(
                hex[byte & 0x0f]);
        }

        return result;
    }

    std::string hash =
        sha256_file(path);

    if (hash.size() != 64)
        fail(
            "invalid SHA-256 for: " +
            path.string());

    for (std::size_t i = 0;
         i < 32;
         ++i) {

        auto hex_value =
            [](char c) -> std::uint8_t {
                if (c >= '0' && c <= '9')
                    return static_cast<std::uint8_t>(
                        c - '0');

                if (c >= 'a' && c <= 'f')
                    return static_cast<std::uint8_t>(
                        c - 'a' + 10);

                if (c >= 'A' && c <= 'F')
                    return static_cast<std::uint8_t>(
                        c - 'A' + 10);

                fail("invalid SHA-256");
            };

        record.digest[i] =
            static_cast<std::uint8_t>(
                (hex_value(hash[i * 2]) << 4) |
                hex_value(hash[i * 2 + 1]));
    }

    record.mtime_seconds =
        seconds;

    record.mtime_nanoseconds =
        nanoseconds;

    return hash;
}

bool hash_equals(
    const HashRecord &record,
    const std::string &hash) {

    auto decoded =
        decode_hash(hash);

    return std::memcmp(
        record.digest,
        decoded.data(),
        32) == 0;
}

void store_hash(
    HashRecord &record,
    const std::string &hash) {

    auto decoded =
        decode_hash(hash);

    std::memcpy(
        record.digest,
        decoded.data(),
        32);
}

std::string hash_hex(
    const HashRecord &record) {

    static constexpr char hex[] =
        "0123456789abcdef";

    std::string result;
    result.reserve(64);

    for (uint8_t byte :
         record.digest) {

        result +=
            hex[(byte >> 4) & 0xf];

        result +=
            hex[byte & 0xf];
    }

    return result;
}

size_t align_up(
    size_t value,
    size_t alignment) {

    return (
        value +
        alignment -
        1
    ) & ~(
        alignment -
        1);
}

std::string make_input_hash(
    const std::vector<fs::path> &link_inputs) {

    std::ostringstream fingerprint;

    fingerprint
        << "nightbuild-inputs-v1\n";

    for (const auto &object :
         link_inputs) {

        fingerprint
            << object.string()
            << "\n";

        if (!file_exists(object)) {
            fingerprint
                << "<MISSING>\n";

            continue;
        }

        fingerprint
            << sha256_file(object)
            << "\n";
    }

    return sha256_text(
        fingerprint.str());
}

std::vector<fs::path> parse_dependency_file(
    const BuildPaths &paths,
    const std::string &source,
    const fs::path &dependency) {

    if (!file_exists(dependency))
        return {};

    std::string content =
        read_file(dependency);

    std::string rule;

    for (size_t i = 0;
         i < content.size();
         ++i) {

        char c = content[i];

        if (c == '\\') {
            if (i + 1 < content.size() &&
                content[i + 1] == '\n') {

                ++i;
                rule += ' ';
                continue;
            }

            if (i + 2 < content.size() &&
                content[i + 1] == '\r' &&
                content[i + 2] == '\n') {

                i += 2;
                rule += ' ';
                continue;
            }
        }

        if (c == '\n' || c == '\r')
            break;

        rule += c;
    }

    if (rule.empty())
        return {};

    bool escaped = false;
    size_t colon =
        std::string::npos;

    for (size_t i = 0;
         i < rule.size();
         ++i) {

        char c = rule[i];

        if (escaped) {
            escaped = false;
            continue;
        }

        if (c == '\\') {
            escaped = true;
            continue;
        }

        if (c == ':') {
            colon = i;
            break;
        }
    }

    if (colon == std::string::npos)
        return {};

    std::string dependency_text =
        rule.substr(colon + 1);

    std::vector<std::string> words;

    {
        std::string current;
        bool escaped_word = false;

        for (char c :
             dependency_text) {

            if (escaped_word) {
                current += c;
                escaped_word = false;
                continue;
            }

            if (c == '\\') {
                escaped_word = true;
                continue;
            }

            if (std::isspace(
                    static_cast<unsigned char>(c))) {

                if (!current.empty()) {
                    words.push_back(
                        current);

                    current.clear();
                }

                continue;
            }

            current += c;
        }

        if (!current.empty())
            words.push_back(current);
    }

    std::vector<fs::path> result;

    fs::path source_file =
        source_path(
            paths,
            source);

    fs::path object =
        object_path(
            paths,
            source);

    for (const auto &word :
         words) {

        fs::path value(word);

        if (value == object)
            continue;

        if (value.is_relative()) {
            value =
                fs::absolute(
                    dependency.parent_path() /
                    value);
        }

        value =
            value.lexically_normal();

        result.push_back(value);
    }

    if (std::find(
            result.begin(),
            result.end(),
            source_file) == result.end()) {

        result.push_back(
            source_file);
    }

    std::sort(
        result.begin(),
        result.end());

    result.erase(
        std::unique(
            result.begin(),
            result.end()),
        result.end());

    return result;
}

std::string dependency_hash(
    const BuildPaths &paths,
    const std::string &source,
    const fs::path &dependency) {

    auto dependencies =
        parse_dependency_file(
            paths,
            source,
            dependency);

    if (dependencies.empty())
        return {};

    std::ostringstream fingerprint;

    fingerprint
        << "nightbuild-dependencies-v3\n";

    for (const auto &path :
         dependencies) {

        fingerprint
            << path.string()
            << "\n";

        if (!file_exists(path)) {
            fingerprint
                << "<MISSING>\n";

            continue;
        }

        fingerprint
            << sha256_file(path)
            << "\n";
    }

    return sha256_text(
        fingerprint.str());
}

std::string hash_for_arguments(
    const std::vector<std::string> &args) {

    std::ostringstream fingerprint;

    fingerprint
        << "nightbuild-command-v1\n";

    for (const auto &arg :
         args) {

        fingerprint
            << arg.size()
            << ":"
            << arg
            << "\n";
    }

    return sha256_text(
        fingerprint.str());
}

uint64_t add_string(
    std::vector<char> &strings,
    const std::string &value) {

    uint64_t offset =
        static_cast<uint64_t>(
            strings.size());

    strings.insert(
        strings.end(),
        value.begin(),
        value.end());

    strings.push_back('\0');

    return offset;
}

uint64_t add_hash(
    std::vector<HashRecord> &hashes,
    const std::string &value) {

    HashRecord record{};

    if (!value.empty())
        store_hash(
            record,
            value);

    uint64_t index =
        static_cast<uint64_t>(
            hashes.size());

    hashes.push_back(record);

    return index;
}

void append_command_database(
    const fs::path &destination,
    const std::vector<BuildRecord> &records,
    const std::string &manifest_hash) {

    std::vector<CommandRecord> commands;
    std::vector<uint64_t> arguments;
    std::vector<HashRecord> hashes;
    std::vector<char> strings;

    uint64_t manifest_hash_offset =
        add_string(
            strings,
            manifest_hash);

    for (const auto &record :
         records) {

        CommandRecord command{};

        command.kind =
            static_cast<uint32_t>(
                record.kind);

        command.target_name_offset =
            add_string(
                strings,
                record.target);

        command.argc =
            record.argv.size();

        command.argv_offset =
            arguments.size();

        for (const auto &arg :
             record.argv) {

            arguments.push_back(
                add_string(
                    strings,
                    arg));
        }

        command.description_offset =
            add_string(
                strings,
                record.description);

        command.source_path_offset =
            add_string(
                strings,
                record.source);

        command.output_path_offset =
            add_string(
                strings,
                record.output);

        command.dependency_path_offset =
            add_string(
                strings,
                record.dependency);

        command.source_hash_index =
            add_hash(
                hashes,
                record.source_hash);

        command.dependency_hash_index =
            add_hash(
                hashes,
                record.dependency_hash);

        command.input_hash_index =
            add_hash(
                hashes,
                record.input_hash);

        command.state_hash_index =
            add_hash(
                hashes,
                record.state_hash);

        commands.push_back(command);
    }

    size_t offset =
        sizeof(CommandFileHeader);

    offset =
        align_up(
            offset,
            alignof(CommandRecord));

    size_t command_offset =
        offset;

    offset +=
        commands.size() *
        sizeof(CommandRecord);

    offset =
        align_up(
            offset,
            alignof(uint64_t));

    size_t argument_offset =
        offset;

    offset +=
        arguments.size() *
        sizeof(uint64_t);

    offset =
        align_up(
            offset,
            alignof(HashRecord));

    size_t hash_offset =
        offset;

    offset +=
        hashes.size() *
        sizeof(HashRecord);

    size_t string_offset =
        offset;

    offset +=
        strings.size();

    size_t file_size =
        offset;

    std::vector<uint8_t> image(
        file_size,
        0);

    CommandFileHeader header{};

    std::memcpy(
        header.magic,
        COMMAND_MAGIC,
        sizeof(COMMAND_MAGIC));

    header.version =
        COMMAND_VERSION;

    header.file_size =
        file_size;

    header.command_count =
        commands.size();

    header.command_offset =
        command_offset;

    header.argument_count =
        arguments.size();

    header.argument_offset =
        argument_offset;

    header.hash_count =
        hashes.size();

    header.hash_offset =
        hash_offset;

    header.string_offset =
        string_offset;

    header.string_size =
        strings.size();

    header.manifest_hash_offset =
        manifest_hash_offset;

    std::memcpy(
        image.data(),
        &header,
        sizeof(header));

    if (!commands.empty()) {
        std::memcpy(
            image.data() + command_offset,
            commands.data(),
            commands.size() *
                sizeof(CommandRecord));
    }

    if (!arguments.empty()) {
        std::memcpy(
            image.data() + argument_offset,
            arguments.data(),
            arguments.size() *
                sizeof(uint64_t));
    }

    if (!hashes.empty()) {
        std::memcpy(
            image.data() + hash_offset,
            hashes.data(),
            hashes.size() *
                sizeof(HashRecord));
    }

    if (!strings.empty()) {
        std::memcpy(
            image.data() + string_offset,
            strings.data(),
            strings.size());
    }

    fs::path temporary =
        destination;

    temporary += ".new";

    fs::create_directories(
        destination.parent_path());

    int fd =
        ::open(
            temporary.c_str(),
            O_WRONLY |
            O_CREAT |
            O_TRUNC,
            0644);

    if (fd < 0)
        fail(
            "cannot create command database: " +
            temporary.string());

    size_t written_total = 0;

    while (written_total < image.size()) {
        ssize_t written =
            write(
                fd,
                image.data() + written_total,
                image.size() - written_total);

        if (written < 0) {
            if (errno == EINTR)
                continue;

            close(fd);
            unlink(temporary.c_str());

            fail(
                "failed writing command database");
        }

        if (written == 0) {
            close(fd);
            unlink(temporary.c_str());

            fail(
                "failed writing command database");
        }

        written_total +=
            static_cast<size_t>(written);
    }

    if (fsync(fd) != 0) {
        close(fd);
        unlink(temporary.c_str());

        fail(
            "failed syncing command database");
    }

    close(fd);

    if (rename(
            temporary.c_str(),
            destination.c_str()) != 0) {

        unlink(temporary.c_str());

        fail(
            "failed installing command database");
    }
}

CommandDatabase mmap_commands(
    const fs::path &path) {

    CommandDatabase database;

    database.fd =
        ::open(
            path.c_str(),
            O_RDONLY);

    if (database.fd < 0)
        fail(
            "cannot open command database: " +
            path.string());

    struct stat st{};

    if (fstat(
            database.fd,
            &st) != 0) {

        database.close();

        fail(
            "cannot stat command database");
    }

    if (st.st_size <
        static_cast<off_t>(
            sizeof(CommandFileHeader))) {

        database.close();

        fail(
            "command database is too small");
    }

    database.size =
        static_cast<size_t>(
            st.st_size);

    database.mapping =
        mmap(
            nullptr,
            database.size,
            PROT_READ,
            MAP_PRIVATE,
            database.fd,
            0);

    if (database.mapping == MAP_FAILED) {
        database.close();

        fail(
            "mmap failed for command database");
    }

    database.header =
        static_cast<const CommandFileHeader *>(
            database.mapping);

    const auto &header =
        *database.header;

    if (std::memcmp(
            header.magic,
            COMMAND_MAGIC,
            sizeof(COMMAND_MAGIC)) != 0) {

        database.close();

        fail(
            "invalid command database magic");
    }

    if (header.version !=
        COMMAND_VERSION) {

        database.close();

        fail(
            "unsupported command database version " +
            std::to_string(header.version));
    }

    if (header.file_size !=
        database.size) {

        database.close();

        fail(
            "command database size mismatch");
    }

    auto range_valid =
        [&](uint64_t offset,
            uint64_t count,
            uint64_t element_size) {

        if (element_size == 0)
            return false;

        if (offset > database.size)
            return false;

        if (count >
            (database.size - offset) /
                element_size) {

            return false;
        }

        return true;
    };

    if (!range_valid(
            header.command_offset,
            header.command_count,
            sizeof(CommandRecord)) ||

        !range_valid(
            header.argument_offset,
            header.argument_count,
            sizeof(uint64_t)) ||

        !range_valid(
            header.hash_offset,
            header.hash_count,
            sizeof(HashRecord)) ||

        header.string_offset >
            database.size ||

        header.string_size >
            database.size -
                header.string_offset) {

        database.close();

        fail(
            "invalid command database offsets");
    }

    if (header.manifest_hash_offset >=
        header.string_size) {

        database.close();

        fail(
            "invalid manifest hash offset");
    }

    database.commands =
        reinterpret_cast<
            const CommandRecord *>(
                static_cast<const uint8_t *>(
                    database.mapping) +
                header.command_offset);

    database.arguments =
        reinterpret_cast<
            const uint64_t *>(
                static_cast<const uint8_t *>(
                    database.mapping) +
                header.argument_offset);

    database.hashes =
        reinterpret_cast<
            const HashRecord *>(
                static_cast<const uint8_t *>(
                    database.mapping) +
                header.hash_offset);

    database.strings =
        reinterpret_cast<
            const char *>(
                static_cast<const uint8_t *>(
                    database.mapping) +
                header.string_offset);

    return database;
}

const char *database_string(
    const CommandDatabase &database,
    uint64_t offset) {

    if (offset >=
        database.header->string_size) {

        fail("invalid string offset");
    }

    const char *value =
        database.strings + offset;

    size_t remaining =
        database.header->string_size - offset;

    if (std::memchr(
            value,
            '\0',
            remaining) == nullptr) {

        fail(
            "unterminated string in command database");
    }

    return value;
}

std::string database_string_copy(
    const CommandDatabase &database,
    uint64_t offset) {

    return std::string(
        database_string(
            database,
            offset));
}

std::vector<const char *> mapped_argv(
    const CommandDatabase &database,
    const CommandRecord &command) {

    if (command.argv_offset >
        database.header->argument_count) {

        fail("invalid argv offset");
    }

    if (command.argc >
        database.header->argument_count -
            command.argv_offset) {

        fail("invalid argv count");
    }

    std::vector<const char *> argv;

    argv.reserve(
        command.argc + 1);

    for (uint64_t i = 0;
         i < command.argc;
         ++i) {

        uint64_t string_offset =
            database.arguments[
                command.argv_offset + i];

        argv.push_back(
            database_string(
                database,
                string_offset));
    }

    argv.push_back(nullptr);

    return argv;
}

std::vector<std::string> mapped_command_copy(
    const CommandDatabase &database,
    const CommandRecord &command) {

    if (command.argv_offset >
        database.header->argument_count) {

        fail("invalid argv offset");
    }

    if (command.argc >
        database.header->argument_count -
            command.argv_offset) {

        fail("invalid argv count");
    }

    std::vector<std::string> result;

    result.reserve(command.argc);

    for (uint64_t i = 0;
         i < command.argc;
         ++i) {

        result.emplace_back(
            database_string(
                database,
                database.arguments[
                    command.argv_offset + i]));
    }

    return result;
}

int run_mapped_command(
    const CommandDatabase &database,
    const CommandRecord &command) {

    if (command.argc == 0)
        fail("empty mapped command");

    std::vector<const char *> argv =
        mapped_argv(
            database,
            command);

    pid_t pid = fork();

    if (pid < 0)
        fail("fork failed");

    if (pid == 0) {
        execvp(
            argv[0],
            const_cast<char *const *>(
                argv.data()));

        _exit(127);
    }

    int status = 0;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;

        fail("waitpid failed");
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);

    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);

    return 1;
}

MutableCommandDatabase mmap_commands_mutable(
    const fs::path &path) {

    MutableCommandDatabase database;

    database.fd =
        ::open(
            path.c_str(),
            O_RDWR);

    if (database.fd < 0)
        fail(
            "cannot open command database for update: " +
            path.string());

    struct stat st{};

    if (fstat(
            database.fd,
            &st) != 0) {

        database.close();

        fail(
            "cannot stat command database");
    }

    database.size =
        static_cast<size_t>(
            st.st_size);

    if (database.size <
        sizeof(CommandFileHeader)) {

        database.close();

        fail(
            "command database is too small");
    }

    database.mapping =
        mmap(
            nullptr,
            database.size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            database.fd,
            0);

    if (database.mapping == MAP_FAILED) {
        database.close();

        fail(
            "mmap failed for writable command database");
    }

    database.header =
        static_cast<CommandFileHeader *>(
            database.mapping);

    if (std::memcmp(
            database.header->magic,
            COMMAND_MAGIC,
            sizeof(COMMAND_MAGIC)) != 0) {

        database.close();

        fail(
            "invalid command database magic");
    }

    if (database.header->version !=
        COMMAND_VERSION) {

        database.close();

        fail(
            "unsupported command database version");
    }

    if (database.header->file_size !=
        database.size) {

        database.close();

        fail(
            "command database size mismatch");
    }

    auto range_valid =
        [&](uint64_t offset,
            uint64_t count,
            uint64_t element_size) {

        if (element_size == 0)
            return false;

        if (offset > database.size)
            return false;

        if (count >
            (database.size - offset) /
                element_size) {

            return false;
        }

        return true;
    };

    if (!range_valid(
            database.header->command_offset,
            database.header->command_count,
            sizeof(CommandRecord)) ||

        !range_valid(
            database.header->argument_offset,
            database.header->argument_count,
            sizeof(uint64_t)) ||

        !range_valid(
            database.header->hash_offset,
            database.header->hash_count,
            sizeof(HashRecord)) ||

        database.header->string_offset >
            database.size ||

        database.header->string_size >
            database.size -
                database.header->string_offset ||

        database.header->manifest_hash_offset >=
            database.header->string_size) {

        database.close();

        fail(
            "invalid writable command database offsets");
    }

    database.commands =
        reinterpret_cast<CommandRecord *>(
            static_cast<uint8_t *>(
                database.mapping) +
            database.header->command_offset);

    database.arguments =
        reinterpret_cast<uint64_t *>(
            static_cast<uint8_t *>(
                database.mapping) +
            database.header->argument_offset);

    database.hashes =
        reinterpret_cast<HashRecord *>(
            static_cast<uint8_t *>(
                database.mapping) +
            database.header->hash_offset);

    database.strings =
        reinterpret_cast<char *>(
            static_cast<uint8_t *>(
                database.mapping) +
            database.header->string_offset);

    return database;
}

char *mutable_string(
    MutableCommandDatabase &database,
    uint64_t offset) {

    if (offset >=
        database.header->string_size) {

        fail("invalid mutable string offset");
    }

    char *value =
        database.strings + offset;

    size_t remaining =
        database.header->string_size - offset;

    if (std::memchr(
            value,
            '\0',
            remaining) == nullptr) {

        fail(
            "unterminated mutable string in command database");
    }

    return value;
}

void update_hash(
    MutableCommandDatabase &database,
    uint64_t hash_index,
    const std::string &hash) {

    if (hash_index >=
        database.header->hash_count) {

        fail("invalid hash index");
    }

    if (hash.empty())
        return;

    store_hash(
        database.hashes[hash_index],
        hash);
}

std::string mapped_hash(
    const CommandDatabase &database,
    uint64_t index) {

    if (index >=
        database.header->hash_count) {

        fail("invalid hash index");
    }

    return hash_hex(
        database.hashes[index]);
}

bool command_hash_matches(
    const CommandDatabase &database,
    const CommandRecord &command,
    uint64_t hash_index,
    const std::string &current) {

    (void)command;

    if (current.empty())
        return false;

    if (hash_index >=
        database.header->hash_count) {

        return false;
    }

    return hash_equals(
        database.hashes[hash_index],
        current);
}

void run_pre_actions_mapped(
    const CommandDatabase &database) {

    for (uint64_t i = 0;
         i < database.header->command_count;
         ++i) {

        const auto &command =
            database.commands[i];

        if (static_cast<CommandKind>(
                command.kind) !=
            CommandKind::PreAction) {

            continue;
        }

        int result =
            run_mapped_command(
                database,
                command);

        if (result != 0) {
            fail(
                "pre_action exited with status " +
                std::to_string(result));
        }
    }
}

std::string command_source(
    const CommandDatabase &database,
    const CommandRecord &command) {

    return database_string_copy(
        database,
        command.source_path_offset);
}

std::string command_output(
    const CommandDatabase &database,
    const CommandRecord &command) {

    return database_string_copy(
        database,
        command.output_path_offset);
}

std::string command_dependency(
    const CommandDatabase &database,
    const CommandRecord &command) {

    return database_string_copy(
        database,
        command.dependency_path_offset);
}

bool compile_command_stale(
    const BuildPaths &paths,
    const CommandDatabase &database,
    const CommandRecord &command,
    const std::vector<fs::path> &changed_paths) {

    std::string source =
        command_source(
            database,
            command);

    std::string output =
        command_output(
            database,
            command);

    fs::path source_file(source);
    fs::path output_file(output);

    if (!file_exists(source_file))
        return true;

    if (!file_exists(output_file))
        return true;

    const fs::path normalized_source =
        source_file.is_absolute()
            ? source_file.lexically_normal()
            : (paths.root / source_file).lexically_normal();

    for (const fs::path &changed : changed_paths) {
        const fs::path normalized_changed =
            changed.is_absolute()
                ? changed.lexically_normal()
                : (paths.root / changed).lexically_normal();

        if (normalized_changed == normalized_source)
            return true;
    }

    std::string current_source_hash =
        sha256_file(source_file);

    if (!command_hash_matches(
            database,
            command,
            command.source_hash_index,
            current_source_hash)) {

        return true;
    }

    std::string dependency =
        command_dependency(
            database,
            command);

    if (!dependency.empty()) {

        fs::path dependency_file(dependency);

        if (!file_exists(dependency_file))
            return true;

        std::string current_dependency_hash =
            dependency_hash(
                paths,
                source,
                dependency_file);

        if (current_dependency_hash.empty())
            return true;

        if (!command_hash_matches(
                database,
                command,
                command.dependency_hash_index,
                current_dependency_hash)) {

            return true;
        }
    }

    std::vector<std::string> args =
        mapped_command_copy(
            database,
            command);

    std::string current_state_hash =
        hash_for_arguments(args);

    if (!command_hash_matches(
            database,
            command,
            command.state_hash_index,
            current_state_hash)) {

        return true;
    }

    return false;
}

void update_compile_hashes(
    MutableCommandDatabase &database,
    const BuildPaths &paths,
    const CommandRecord &command) {

    fs::path source_path_value =
        mutable_string(
            database,
            command.source_path_offset);

    fs::path dependency_path_value =
        mutable_string(
            database,
            command.dependency_path_offset);

    std::string source_hash =
        sha256_file(
            source_path_value);

    update_hash(
        database,
        command.source_hash_index,
        source_hash);

    if (!dependency_path_value.empty()) {

        std::string dependency_hash_value =
            dependency_hash(
                paths,
                source_path_value.string(),
                dependency_path_value);

        if (!dependency_hash_value.empty()) {
            update_hash(
                database,
                command.dependency_hash_index,
                dependency_hash_value);
        }
    }

    std::vector<std::string> args;

    for (uint64_t i = 0;
         i < command.argc;
         ++i) {

        uint64_t offset =
            database.arguments[
                command.argv_offset + i];

        args.emplace_back(
            mutable_string(
                database,
                offset));
    }

    update_hash(
        database,
        command.state_hash_index,
        hash_for_arguments(args));

    msync(
        database.mapping,
        database.size,
        MS_ASYNC);
}

std::string generation_configuration_hash(
    const BuildPaths &paths,
    const toml::Target &target,
    const fs::path &manifest) {

    std::ostringstream fingerprint;

    fingerprint
        << "nightbuild-configuration-v2\n";

    fingerprint
        << sha256_file(manifest)
        << "\n";

    for (const auto &include :
         target.toolchain_include) {

        fs::path path(include);

        if (path.is_relative())
            path =
                paths.root /
                path;

        path =
            fs::absolute(
                path).lexically_normal();

        fingerprint
            << path.string()
            << "\n";

        if (!file_exists(path)) {
            fingerprint
                << "<MISSING>\n";
            continue;
        }

        fingerprint
            << sha256_file(path)
            << "\n";
    }

    return sha256_text(
        fingerprint.str());
}

bool source_is_generated(
    const fs::path &root,
    const std::string &source,
    const std::vector<const toml::Target *> &dependencies) {

    const fs::path source_path =
        root / source;

    if (file_exists(source_path))
        return false;

    for (const toml::Target *dependency :
         dependencies) {

        if (dependency == nullptr)
            continue;

        if (dependency->type != "generator")
            continue;

        if (dependency->output.empty())
            continue;

        if (fs::path(dependency->output) ==
            fs::path(source)) {

            return true;
        }

        for (const std::string &output :
             dependency->outputs) {

            if (fs::path(output) ==
                fs::path(source)) {

                return true;
            }
        }
    }

    return false;
}

std::vector<BuildRecord> make_build_records(
    const BuildPaths &paths,
    const toml::Target &target,
    const PkgConfigFlags &pkg,
    const std::string &manifest_hash,
    const std::vector<Toolchain> &toolchains, const std::vector<const toml::Target *> &dependencies) {

    (void)manifest_hash;

    std::vector<BuildRecord> records;

    for (const auto &action :
         target.pre_actions) {

        if (action.empty())
            fail(
                "pre_action command cannot be empty");

        BuildRecord record;
        record.target = target.name;

        record.kind =
            CommandKind::PreAction;

        record.argv =
            action;

        record.description =
            "PRE";

        records.push_back(
            std::move(record));
    }

    OutputType type =
        parse_output_type(
            target.type);

    if (type == OutputType::Action) {
        if (target.action.empty())
            fail(
                "ACTION target requires an action");

        BuildRecord record;
        record.target = target.name;
        record.kind =
            CommandKind::Action;
        record.argv =
            target.action;
        record.description =
            "ACTION " +
            target.name;
        record.output =
            target.output;
        record.state_hash =
            hash_for_arguments(
                record.argv);

        records.push_back(
            std::move(record));
        return records;
    }

    if (type == OutputType::Object) {
        if (target.sources.size() != 1)
            fail(
                "object target requires exactly one source");

        const std::string &source =
            target.sources.front();

        const Toolchain *toolchain =
            find_toolchain(
                toolchains,
                source);

        if (toolchain == nullptr)
            fail(
                "no toolchain registered for source '" +
                source +
                "'");

        BuildRecord record;
        record.target = target.name;
        record.kind =
            CommandKind::Object;

        record.argv =
            object_command(
                paths,
                target,
                pkg,
                toolchains);

        record.description =
            toolchain->description +
            " " +
            target.output;

        record.source =
            source_path(
                paths,
                source).string();

        record.output =
            (
                paths.root /
                target.output
            ).string();

        record.source_hash =
            sha256_file(
                record.source);

        record.state_hash =
            hash_for_arguments(
                record.argv);

        records.push_back(
            std::move(record));

        return records;
    }

    for (const auto &source :
         target.sources) {

        const Toolchain *toolchain =
            find_toolchain(
                toolchains,
                source);

        if (toolchain == nullptr)
            fail(
                "no toolchain registered for source '" +
                source +
                "'. Add a toolchain JSON file to "
                "'toolchain_include'.");

        BuildRecord record;
        record.target = target.name;

        record.kind =
            CommandKind::Compile;

        record.argv =
            compile_command(
                paths,
                target,
                source,
                pkg,
                toolchains);

        record.description =
            toolchain->description +
            " " +
            source;

        record.source =
            source_path(
                paths,
                source).string();

        record.output =
            toolchain_output_path(
                paths,
                source,
                *toolchain).string();

        record.dependency =
            toolchain_dependency_path(
                paths,
                source).string();

        if (file_exists(
                record.source)) {

            record.source_hash =
                sha256_file(
                    record.source);

        } else if (!source_is_generated(
                    paths.root,
                    source,
                    dependencies)) {

            fail(
                "failed to open source file for hashing: " +
                record.source);
        }

        if (file_exists(
                record.dependency)) {

            record.dependency_hash =
                dependency_hash(
                    paths,
                    source,
                    record.dependency);
        }

        record.state_hash =
            hash_for_arguments(
                record.argv);

        records.push_back(
            std::move(record));
    }

    std::vector<fs::path> objects =
        target_objects(
            paths,
            target,
            toolchains);

    std::vector<fs::path> link_inputs =
        objects;

    for (const toml::Target *dependency :
         dependencies) {
        if (dependency->type == "generator")
            continue;


        link_inputs.push_back(
            paths.root /
            dependency->output);
    }

    BuildRecord link;
    link.target = target.name;

    switch (type) {
    case OutputType::Generator:
        link.kind =
            CommandKind::Link;
        link.argv =
            generator_link_command(
                paths,
                target,
                link_inputs,
                pkg);
        link.description =
            "LINK generator/" +
            target.name;
        link.output =
            (paths.build /
             "generators" /
             target.name).string();
        break;

    case OutputType::Executable:
        link.kind =
            CommandKind::Link;

        link.argv =
            executable_link_command(
                paths,
                target,
                link_inputs,
                pkg);

        link.description =
            "LINK " +
            target.output;

        link.output =
            (
                paths.root /
                target.output
            ).string();

        break;

    case OutputType::App:
        link.kind =
            CommandKind::Link;

        link.argv =
            app_link_command(
                paths,
                target,
                link_inputs,
                pkg);

        link.description =
            "APP " +
            target.output;

        link.output =
            app_bundle_path(
                paths,
                target).string();

        break;

    case OutputType::StaticLib:
        link.kind =
            CommandKind::Link;

        link.argv =
            static_library_command(
                paths,
                target,
                link_inputs);

        link.description =
            "AR " +
            target.output;

        link.output =
            (
                paths.root /
                target.output
            ).string();

        break;

    case OutputType::SharedLib:
        link.kind =
            CommandKind::Link;

        link.argv =
            shared_library_command(
                paths,
                target,
                link_inputs,
                pkg);

        link.description =
            "LINK " +
            target.output;

        link.output =
            (
                paths.root /
                target.output
            ).string();

        break;

    case OutputType::Framework:
        link.kind =
            CommandKind::Link;

        link.argv =
            framework_command(
                paths,
                target,
                link_inputs,
                pkg);

        link.description =
            "FRAMEWORK " +
            target.output;

        link.output =
            (
                paths.root /
                target.output
            ).string();

        break;

    default:
        return records;
    }

    link.input_hash =
        make_input_hash(
            link_inputs);

    link.state_hash =
        hash_for_arguments(
            link.argv);

    records.push_back(
        std::move(link));

    if (type == OutputType::Generator) {
        BuildRecord generator;
        generator.target = target.name;
        generator.kind =
            CommandKind::Generator;
        generator.argv.push_back(
            (paths.build /
             "generators" /
             target.name).string());
        generator.argv.insert(
            generator.argv.end(),
            target.action.begin(),
            target.action.end());
        generator.description =
            "GEN " +
            target.name;
        generator.output =
            (paths.root /
             target.output).string();
        generator.state_hash =
            hash_for_arguments(
                generator.argv);

        records.push_back(
            std::move(generator));
    }


    return records;
}

std::vector<std::string> resolve_target_order(
    const toml::Project &project) {

    std::vector<std::string> target_names;

    for (const auto &[name, target] :
         project.targets) {

        (void)target;

        target_names.push_back(name);
    }

    std::sort(
        target_names.begin(),
        target_names.end());

    std::unordered_map<
        std::string,
        int>
        visit_state;

    std::vector<std::string>
        ordered_targets;

    std::function<void(const std::string &)>
        visit =
        [&](const std::string &name) {

        int &state =
            visit_state[name];

        if (state == 2)
            return;

        if (state == 1) {
            fail(
                "dependency cycle involving target: " +
                name);
        }

        state = 1;

        const toml::Target &target =
            project.targets.at(name);

        std::vector<std::string>
            dependencies =
                target.dependencies;

        std::sort(
            dependencies.begin(),
            dependencies.end());

        for (const std::string &dependency :
             dependencies) {

            if (project.targets.find(
                    dependency) ==
                project.targets.end()) {

                fail(
                    "target '" +
                    name +
                    "' depends on unknown target '" +
                    dependency +
                    "'");
            }

            visit(dependency);
        }

        state = 2;

        ordered_targets.push_back(name);
    };

    for (const std::string &name :
         target_names) {

        visit(name);
    }

    return ordered_targets;
}

void generate(
    const fs::path &root,
    const fs::path &build_dir) {

    BuildPaths paths =
        make_paths(build_dir);
    

    paths.root =
        fs::absolute(root);

    FSEventsWatcher fsevents(paths.root);
    fsevents.start();

    fs::path manifest =
        fs::absolute(root) /
        "BUILD.nb";

    if (!file_exists(manifest))
        fail(
            "BUILD.nb not found: " +
            manifest.string());

    toml::Project project =
        toml::parse_file(
            manifest.string(),
            paths.build);

    fs::create_directories(
        paths.build);

    fs::create_directories(
        paths.objects);

    
    

    std::vector<std::string> ordered_targets =
        resolve_target_order(project);

    std::vector<BuildRecord> all_records;
    std::ostringstream project_fingerprint;
    project_fingerprint << "configuration:"
                    << project.configuration_hash
                    << '\n';

    for (const std::string &name :
        ordered_targets) {

        const toml::Target &target =
            project.targets.at(name);

        std::vector<Toolchain> toolchains =
            load_toolchains(
                paths,
                target);

        PkgConfigFlags pkg =
            resolve_pkg_config(
                target);

        std::string target_hash =
            generation_configuration_hash(
                paths,
                target,
                manifest);

        project_fingerprint
            << name
            << "\n"
            << target_hash
            << "\n";

        /*
         * Existing record generation remains
         * completely unchanged.
         */
        std::vector<const toml::Target *> dependencies;

        for (const std::string &dependency_name :
            target.dependencies) {
            dependencies.push_back(
                &project.targets.at(dependency_name));
        }

        for (const std::string &generator_name :
            target.generator_deps) {
            dependencies.push_back(
                &project.targets.at(generator_name));
        }

        std::vector<BuildRecord> records =
            make_build_records(
                paths,
                target,
                pkg,
                target_hash,
                toolchains,
                dependencies);

        all_records.insert(
            all_records.end(),
            records.begin(),
            records.end());
    }

    std::string project_hash =
        project_fingerprint.str();

    append_command_database(
        paths.commands,
        all_records,
        project_hash);
    
    // write_compile_commands(paths, project.default_targets);
}

bool manifest_changed(
    const BuildPaths &paths,
    const fs::path &manifest,
    const CommandDatabase &database) {

    toml::Project project =
        toml::parse_file(
            manifest.string(),
            paths.build);

    std::ostringstream fingerprint;

    fingerprint << "configuration:"
                << project.configuration_hash
                << '\n';

    std::vector<std::string> ordered_targets =
        resolve_target_order(project);

    for (const std::string &name :
         ordered_targets) {

        const toml::Target &target =
            project.targets.at(name);

        fingerprint
            << name
            << "\n"
            << generation_configuration_hash(
                paths,
                target,
                manifest)
            << "\n";
    }

    std::string current =
        sha256_text(
            fingerprint.str());

    std::string stored =
        database_string_copy(
            database,
            database.header->
                manifest_hash_offset);

    return current != stored;
}
// meow
std::vector<std::string> mutable_command_args(
    MutableCommandDatabase &database,
    const CommandRecord &command) {

    if (command.argv_offset >
        database.header->argument_count) {

        fail("invalid argv offset");
    }

    if (command.argc >
        database.header->argument_count -
            command.argv_offset) {

        fail("invalid argv count");
    }

    std::vector<std::string> args;

    args.reserve(command.argc);

    for (uint64_t j = 0;
         j < command.argc;
         ++j) {

        uint64_t offset =
            database.arguments[
                command.argv_offset + j];

        args.emplace_back(
            mutable_string(
                database,
                offset));
    }

    return args;
}

int build_object_target(
    const BuildPaths &paths,
    MutableCommandDatabase &database) {

    for (uint64_t i = 0;
         i < database.header->command_count;
         ++i) {

        CommandRecord &command =
            database.commands[i];

        if (static_cast<CommandKind>(
                command.kind) !=
            CommandKind::Object) {

            continue;
        }

        fs::path source =
            mutable_string(
                database,
                command.source_path_offset);

        fs::path output =
            mutable_string(
                database,
                command.output_path_offset);

        if (!file_exists(source))
            fail(
                "source not found: " +
                source.string());

        bool stale =
            !file_exists(output);

        if (!stale) {
            std::string current =
                cached_sha256_file(
                    database,
                    command.source_hash_index,
                    source);

            stale =
                !hash_equals(
                    database.hashes[
                        command.source_hash_index],
                    current);
        }

        if (!stale) {
            auto args =
                mutable_command_args(
                    database,
                    command);

            std::string command_hash =
                hash_for_arguments(args);

            stale =
                !hash_equals(
                    database.hashes[
                        command.state_hash_index],
                    command_hash);
        }

        if (!stale) {
            std::cout
                << "nightbuild: no work to do.\n";

            return 0;
        }

        auto args =
            mutable_command_args(
                database,
                command);

        std::cout
            << "[1/1] "
            << mutable_string(
                database,
                command.description_offset)
            << "\n";

        int result =
            run_process(args);

        if (result != 0)
            return result;

        cached_sha256_file(
            database,
            command.source_hash_index,
            source);

        update_hash(
            database,
            command.state_hash_index,
            hash_for_arguments(args));

        msync(
            database.mapping,
            database.size,
            MS_SYNC);

        return 0;
    }

    fail("object command missing");
}

int run_action_target(
    MutableCommandDatabase &database) {

    for (uint64_t i = 0;
         i < database.header->command_count;
         ++i) {

        CommandRecord &command =
            database.commands[i];

        if (static_cast<CommandKind>(
                command.kind) !=
            CommandKind::Action) {

            continue;
        }

        auto args =
            mutable_command_args(
                database,
                command);

        std::string current_hash =
            hash_for_arguments(args);

        if (file_exists(
                mutable_string(
                    database,
                    command.output_path_offset)) &&
            hash_equals(
                database.hashes[
                    command.state_hash_index],
                current_hash)) {

            std::cout
                << "nightbuild: no work to do.\n";

            return 0;
        }

        std::cout
            << "[1/1] "
            << mutable_string(
                database,
                command.description_offset)
            << "\n";

        int result =
            run_process(args);

        if (result != 0)
            return result;

        update_hash(
            database,
            command.state_hash_index,
            current_hash);

        msync(
            database.mapping,
            database.size,
            MS_SYNC);

        return 0;
    }

    fail("action command missing");
}

std::vector<fs::path> link_input_paths(
    const CommandDatabase &database,
    const CommandRecord &command) {

    std::vector<std::string> args =
        mapped_command_copy(
            database,
            command);

    std::vector<fs::path> inputs;

    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-o") {
            ++i;
            continue;
        }

        fs::path path(args[i]);

        if (!file_exists(path))
            continue;

        if (path == command_output(database, command))
            continue;

        inputs.push_back(path);
    }

    return inputs;
}

bool link_needed(
    const BuildPaths &paths,
    const CommandDatabase &database,
    const CommandRecord &command,
    const std::vector<fs::path> &objects) {

    (void)paths;

    fs::path output =
        command_output(
            database,
            command);

    if (!file_exists(output))
        return true;

    std::vector<fs::path> link_inputs =
        objects;

    std::string current_input =
        make_input_hash(link_inputs);

    if (!command_hash_matches(
            database,
            command,
            command.input_hash_index,
            current_input)) {

        return true;
    }

    std::vector<std::string> args =
        mapped_command_copy(
            database,
            command);

    std::string command_hash =
        hash_for_arguments(args);

    if (!command_hash_matches(
            database,
            command,
            command.state_hash_index,
            command_hash)) {

        return true;
    }

    return false;
}

void update_link_record(
    MutableCommandDatabase &database,
    const CommandRecord &command,
    const std::vector<fs::path> &objects) {


    std::vector<fs::path> link_inputs =
        objects;

    std::string input =
        make_input_hash(link_inputs);

    update_hash(
        database,
        command.input_hash_index,
        input);

    std::vector<std::string> args =
        mutable_command_args(
            database,
            command);

    update_hash(
        database,
        command.state_hash_index,
        hash_for_arguments(args));

    msync(
        database.mapping,
        database.size,
        MS_SYNC);
}

int execute_link_command(
    MutableCommandDatabase &database,
    size_t index,
    const std::vector<fs::path> &objects,
    size_t progress,
    size_t total,
    bool verbose) {

    if (index >=
        database.header->command_count) {

        fail("invalid link command index");
    }

    CommandRecord &command =
        database.commands[index];

    if (static_cast<CommandKind>(
            command.kind) !=
        CommandKind::Link) {

        fail("invalid link command");
    }

    auto args =
        mutable_command_args(
            database,
            command);

    prepare_command_outputs(args);

    const std::string description =
        mutable_string(
            database,
            command.description_offset);

    const bool live =
        !verbose &&
        isatty(STDOUT_FILENO);

    auto show_status =
        [&](const std::string &text) {

        if (!live)
            return;

        std::cout
            << "\r\033[K"
            << "NightBuild · "
            << text
            << std::flush;
    };

    if (live) {

        show_status(
            std::to_string(progress) +
            "/" +
            std::to_string(total) +
            " · linking · " +
            description);

    } else {

        std::cout
            << "["
            << progress
            << "/"
            << total
            << "] "
            << description
            << "\n";
    }

    if (verbose) {

        std::cout
            << "  ";

        for (const auto &arg : args) {

            std::cout
                << arg
                << ' ';
        }

        std::cout
            << "\n";
    }

    pid_t pid = fork();

    if (pid < 0)
        fail("fork failed");

    if (pid == 0) {

        std::vector<char *> argv =
            make_argv(args);

        execvp(
            argv[0],
            argv.data());

        _exit(127);
    }

    int status = 0;

    while (waitpid(
               pid,
               &status,
               0) < 0) {

        if (errno == EINTR)
            continue;

        if (live)
            std::cout
                << "\r\033[K"
                << std::flush;

        fail("waitpid failed");
    }

    int result = 1;

    if (WIFEXITED(status))
        result =
            WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        result =
            128 + WTERMSIG(status);

    if (result != 0) {

        if (live)
            std::cout
                << "\r\033[K"
                << std::flush;

        return result;
    }

    update_link_record(
        database,
        command,
        objects);

    if (live) {

        std::cout
            << "\r\033[K"
            << "NightBuild · "
            << progress
            << "/"
            << total
            << " · ✓ "
            << description
            << "\n";
    }

    return 0;
}

int compile_parallel_mapped(
    const BuildPaths &paths,
    MutableCommandDatabase &database,
    const std::vector<size_t> &indices,
    size_t total_commands,
    bool verbose)
{
    if (indices.empty())
        return 0;

    const long cpu_count =
        sysconf(_SC_NPROCESSORS_ONLN);

    const size_t max_jobs =
        cpu_count > 0
            ? static_cast<size_t>(cpu_count)
            : 1;

    const bool live =
        !verbose &&
        isatty(STDOUT_FILENO);

    struct Running {
        pid_t pid;
        size_t index;
        std::string description;
        std::chrono::steady_clock::time_point start;
    };

    auto clear_status =
        [&]() {

        if (!live)
            return;

        std::cout
            << "\r\033[K"
            << std::flush;
    };

    auto show_status =
        [&](const std::string &text) {

        if (!live)
            return;

        std::cout
            << "\r\033[K"
            << "NightBuild · "
            << text
            << std::flush;
    };

    Scheduler scheduler(
        paths,
        database);

    if (live) {
        std::cout
            << "NightBuild · Scheduling...\n"
            << std::flush;
    }

    /*
     * Add every requested command before starting execution.
     *
     * This is important because the scheduler builds its
     * dependency graph from the complete requested set.
     */
    for (size_t index : indices)
        scheduler.add(index);

    std::vector<Running> running;

    running.reserve(
        max_jobs);

    size_t completed = 0;

    while (!scheduler.done()) {

        /*
         * Fill every available CPU slot.
         */
        while (!scheduler.empty() &&
               running.size() < max_jobs) {

            const size_t index =
                scheduler.next();

            if (index ==
                std::numeric_limits<size_t>::max()) {

                break;
            }

            CommandRecord &command =
                database.commands[index];

            auto args =
                mutable_command_args(
                    database,
                    command);

            prepare_command_outputs(
                args);

            const std::string description =
                mutable_string(
                    database,
                    command.description_offset);

            if (verbose) {

                std::cout
                    << "["
                    << (completed +
                        running.size() +
                        1)
                    << "/"
                    << total_commands
                    << "] "
                    << description
                    << "\n";

                std::cout
                    << "  ";

                for (const auto &arg : args)
                    std::cout
                        << arg
                        << ' ';

                std::cout
                    << "\n";

            } else if (live) {

                show_status(
                    std::to_string(completed) +
                    "/" +
                    std::to_string(total_commands) +
                    " · compiling · " +
                    description);
            }

            std::vector<char *> argv =
                make_argv(args);

            const auto start =
                std::chrono::steady_clock::now();

            const pid_t pid =
                fork();

            if (pid < 0) {

                clear_status();

                std::perror("fork");

                return 1;
            }

            if (pid == 0) {
                

                execvp(
                    argv[0],
                    argv.data());

                std::perror(
                    argv[0]);

                _exit(127);
            }

            running.push_back({
                pid,
                index,
                description,
                start
            });
        }

        /*
         * If there is nothing running but work remains, the
         * dependency graph is stuck.
         */
        if (running.empty()) {

            clear_status();

            std::cerr
                << "nightbuild: scheduler deadlock\n";

            return 1;
        }

        int status = 0;

        pid_t pid;

        do {

            pid =
                waitpid(
                    -1,
                    &status,
                    0);

        } while (
            pid < 0 &&
            errno == EINTR);

        if (pid < 0) {

            clear_status();

            std::perror("waitpid");

            return 1;
        }

        const auto it =
            std::find_if(
                running.begin(),
                running.end(),
                [pid](const Running &entry) {

                    return entry.pid == pid;
                });

        if (it == running.end()) {

            clear_status();

            std::cerr
                << "nightbuild: waitpid returned "
                   "unknown child\n";

            return 1;
        }

        const auto finished =
            std::chrono::steady_clock::now();

        const double duration =
            std::chrono::duration<double>(
                finished - it->start)
                .count();

        const size_t command_index =
            it->index;

        CommandRecord &command =
            database.commands[
                command_index];

        const std::string description =
            it->description;

        /*
         * Do not mark the command complete until we know the
         * process actually succeeded.
         */
        if (!WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {

            clear_status();

            if (WIFEXITED(status))
                return WEXITSTATUS(status);

            if (WIFSIGNALED(status))
                return 128 + WTERMSIG(status);

            return 1;
        }

        /*
         * Successful command:
         *
         *   1. Record timing telemetry.
         *   2. Refresh command hashes.
         *   3. Mark the scheduler node complete.
         *   4. This unlocks dependent commands.
         */
        scheduler.record(
            command_index,
            duration);

        const std::string source =
            mutable_string(
                database,
                command.source_path_offset);

        const std::string dependency =
            mutable_string(
                database,
                command.dependency_path_offset);

        if (!source.empty()) {

            update_hash(
                database,
                command.source_hash_index,
                cached_sha256_file(
                    database,
                    command.source_hash_index,
                    source));
        }

        if (!dependency.empty()) {

            update_hash(
                database,
                command.dependency_hash_index,
                dependency_hash(
                    paths,
                    source,
                    dependency));
        }

        update_hash(
            database,
            command.state_hash_index,
            hash_for_arguments(
                mutable_command_args(
                    database,
                    command)));

        scheduler.complete(
            command_index);

        running.erase(it);

        ++completed;

        if (live) {

            show_status(
                std::to_string(completed) +
                "/" +
                std::to_string(total_commands) +
                " · compiling · ✓ " +
                description);
        }
    }

    if (live)
        std::cout
            << "\n";

    msync(
        database.mapping,
        database.size,
        MS_SYNC);

    return 0;
}

int build(
    const fs::path &build_dir,
    const std::vector<std::string> &targets,
    bool verbose)
{
    // int meow = 10;
    // std::cout << meow << std::endl;
    BuildPaths paths =
        make_paths(build_dir);
    
        

    FSEventsWatcher fsevents(paths.root);
    fsevents.start();

    fs::path manifest =
        paths.root /
        "BUILD.nb";

    if (!file_exists(manifest))
        fail(
            "BUILD.nb not found in " +
            paths.root.string());

    if (!file_exists(paths.commands))
        fail(
            "build/commands not found; run "
            "`nightbuild gen` first");

    CommandDatabase database =
        mmap_commands(
            paths.commands);

    if (manifest_changed(
            paths,
            manifest,
            database)) {

        database.close();

        generate(
            paths.root,
            paths.build);

        database =
            mmap_commands(
                paths.commands);
    }

    /*
     * An empty target list means:
     *
     *     build everything
     *
     * Otherwise only commands belonging to
     * the requested targets are considered.
     */
    
    auto selected = [&](const CommandDatabase &db,
                    const CommandRecord &command) {
        if (targets.empty())
            return true;

        std::string command_target =
            database_string_copy(
                db,
                command.target_name_offset);

        return std::find(
            targets.begin(),
            targets.end(),
            command_target) != targets.end();
    };

    // Map every produced artifact to the command that produces it.
    std::unordered_map<std::string, size_t> producers;

    for (uint64_t i = 0;
        i < database.header->command_count;
        ++i) {

        const CommandRecord &command =
            database.commands[i];

        if (command.output_path_offset == 0)
            continue;

        std::string output =
            database_string_copy(
                database,
                command.output_path_offset);

        if (!output.empty())
            producers.emplace(
                std::move(output),
                static_cast<size_t>(i));
    }

    // Expand the selected targets through their artifact dependencies.
    //
    // Example:
    //
    //     hello link
    //        -> libcore.a
    //        -> core link
    //        -> core.o
    //        -> core compile
    //
    std::unordered_set<size_t> required_commands;
    std::vector<size_t> pending;

    for (uint64_t i = 0;
        i < database.header->command_count;
        ++i) {

        const CommandRecord &command =
            database.commands[i];

        if (!selected(database, command))
            continue;

        const size_t index =
            static_cast<size_t>(i);

        if (required_commands.insert(index).second)
            pending.push_back(index);
    }

    while (!pending.empty()) {

        const size_t index =
            pending.back();

        pending.pop_back();

        const CommandRecord &command =
            database.commands[index];

        for (uint64_t a = 0;
            a < command.argc;
            ++a) {

            const uint64_t argument_offset =
                database.arguments[
                    command.argv_offset + a];

            std::string argument =
                database_string_copy(
                    database,
                    argument_offset);

            auto producer =
                producers.find(argument);

            if (producer == producers.end())
                continue;

            const size_t producer_index =
                producer->second;

            if (required_commands.insert(
                    producer_index).second) {

                pending.push_back(
                    producer_index);
            }
        }
    }
    
    /*
     * Pre-actions are currently project-wide.
     *
     * Once pre-actions carry target ownership,
     * this should use the same target filter.
     */
    run_pre_actions_mapped(
        database);

    bool has_object = false;
    bool has_action = false;
    bool has_compile = false;
    bool has_link = false;

    for (uint64_t i = 0;
         i < database.header->command_count;
         ++i) {

        const CommandRecord &command =
            database.commands[i];

        if (!required_commands.contains(
            static_cast<size_t>(i)))
        continue;

        CommandKind kind =
            static_cast<CommandKind>(
                command.kind);

        if (kind == CommandKind::Object)
            has_object = true;

        if (kind == CommandKind::Action)
            has_action = true;

        if (kind == CommandKind::Compile)
            has_compile = true;

        if (kind == CommandKind::Link)
            has_link = true;
    }

    /*
     * Object and ACTION commands still use their
     * existing execution paths for now.
     *
     * Those functions need the same target filter
     * once command ownership is wired into them.
     */
    

    if (has_action) {

        database.close();

        MutableCommandDatabase writable =
            mmap_commands_mutable(
                paths.commands);

        int result =
            run_action_target(
                writable);

        writable.close();

        return result;
    }

    /*
     * Find stale compile commands belonging to
     * the selected targets.
     */
    std::vector<size_t> compile_indices;

    const std::vector<fs::path> changed_paths =
        fsevents.drain();

    for (size_t index : required_commands) {

        const CommandRecord &command =
            database.commands[index];

        if (static_cast<CommandKind>(
                command.kind) !=
            CommandKind::Compile) {

            continue;
        }

        if (compile_command_stale(
                paths,
                database,
                command,
                changed_paths)) {

            compile_indices.push_back(index);
        }
    }

    

    MutableCommandDatabase writable =
        mmap_commands_mutable(
            paths.commands);

    
    /*
     * Count selected link commands that actually
     * need to run.
     */
    
    std::vector<fs::path> objects;

    for (uint64_t i = 0;
        i < writable.header->command_count;
        ++i) {

        const CommandRecord &command =
            writable.commands[i];

        if (!required_commands.contains(
            static_cast<size_t>(i)))
        continue;

        if (static_cast<CommandKind>(
                command.kind) !=
            CommandKind::Compile)
            continue;

        objects.push_back(
            mutable_string(
                writable,
                command.output_path_offset));
    }
    size_t link_count = 0;

    for (uint64_t i = 0;
         i < writable.header->command_count;
         ++i) {

        const CommandRecord &command =
            writable.commands[i];

        if (!required_commands.contains(
            static_cast<size_t>(i)))
        continue;

        if (static_cast<CommandKind>(
                command.kind) !=
            CommandKind::Link) {
            continue;
        }

        if (link_needed(
                paths,
                reinterpret_cast<
                    const CommandDatabase &>(
                    writable),
                command,
                objects)) {
            ++link_count;
        }
    }

    size_t total_work =
        compile_indices.size() +
        link_count;

    /*
     * Nothing to compile and nothing to link.
     */
    if (compile_indices.empty() &&
        !has_link) {

        writable.close();

        std::cout
            << "nightbuild: no work to do.\n";

        return 0;
    }

   
        /*
     * No compilation is required, so check the
     * selected link commands directly.
     */
    if (compile_indices.empty() &&
        has_link) {

        bool did_link = false;
        size_t link_progress = 0;

        for (uint64_t i = 0;
             i < writable.header->command_count;
             ++i) {

            CommandRecord &command =
                writable.commands[i];

            if (!selected(database, command))
            if (!required_commands.contains(static_cast<size_t>(i)))
                continue;
            if (static_cast<CommandKind>(
                    command.kind) !=
                CommandKind::Link) {
                continue;
            }

            CommandDatabase check =
                mmap_commands(
                    paths.commands);

            bool needed =
                link_needed(
                    paths,
                    check,
                    check.commands[i],
                    objects);

            check.close();

            if (!needed)
                continue;

            bool is_app =
                command_output(
                    reinterpret_cast<
                        const CommandDatabase &>(
                        writable),
                    command)
                    .find(".app") !=
                std::string::npos;

            if (is_app) {
                std::string output =
                    mutable_string(
                        writable,
                        command.output_path_offset);

                fs::path app(output);

                fs::create_directories(
                    app /
                    "Contents" /
                    "MacOS");

                fs::path plist =
                    app /
                    "Contents" /
                    "Info.plist";

                toml::Project project =
                    toml::parse_file(
                        (
                            paths.root /
                            "BUILD.nb"
                        ).string(),
                        paths.build);

                write_file(
                    plist,
                    make_app_info_plist(
                        fs::path(output).stem().string()));
            }

            ++link_progress;

            int result =
                execute_link_command(
                    writable,
                    static_cast<size_t>(i),
                    objects,
                    link_progress,
                    total_work,
                    verbose);

            if (result != 0) {
                writable.close();
                return result;
            }

            did_link = true;
        }

        writable.close();

        if (!did_link)
            std::cout
                << "nightbuild: no work to do.\n";

        return 0;
    }
    

    /*
     * Compile all selected stale commands in parallel.
     */
    int result =
        compile_parallel_mapped(
            paths,
            writable,
            compile_indices,
            total_work,
            verbose);

    if (result != 0) {

        writable.close();

        return result;
    }

    writable.close();

    if (!has_compile &&
        !has_link) {

        std::cout
            << "nightbuild: no work to do.\n";

        return 0;
    }

    /*
     * Re-open the database after compilation so
     * we can inspect the updated command state.
     */
    CommandDatabase current =
        mmap_commands(
            paths.commands);

    

    bool did_link = false;

    size_t link_progress =
        compile_indices.size();

    for (uint64_t i = 0;
         i < current.header->command_count;
         ++i) {

        const CommandRecord &command =
            current.commands[i];

        if (!selected(current, command))
        if (!required_commands.contains(static_cast<size_t>(i)))
            continue;

        if (static_cast<CommandKind>(
                command.kind) !=
            CommandKind::Link) {

            continue;
        }

        if (!link_needed(
                paths,
                current,
                command,
                objects)) {

            continue;
        }

        bool is_app =
            command_output(
                current,
                command)
                .find(".app") !=
            std::string::npos;

        current.close();

        MutableCommandDatabase link_db =
            mmap_commands_mutable(
                paths.commands);

        CommandRecord &mutable_command =
            link_db.commands[i];

        if (is_app) {

            std::string output =
                mutable_string(
                    link_db,
                    mutable_command.output_path_offset);

            fs::path app(output);

            fs::create_directories(
                app /
                "Contents" /
                "MacOS");

            fs::path plist =
                app /
                "Contents" /
                "Info.plist";

            toml::Project project =
                toml::parse_file(
                    (
                        paths.root /
                        "BUILD.nb"
                    ).string(),
                    paths.build);

            write_file(
                plist,
                make_app_info_plist(
                    fs::path(output).stem().string()));
        }

        ++link_progress;

        int link_result =
            execute_link_command(
                link_db,
                static_cast<size_t>(i),
                objects,
                link_progress,
                total_work,
                verbose);

        link_db.close();

        if (link_result != 0)
            return link_result;

        current = mmap_commands(paths.commands);

        did_link = true;

        
    }

    if (current.mapping != MAP_FAILED)
        current.close();

    if (!compile_indices.empty() ||
        did_link) {

        return 0;
    }

    std::cout
        << "nightbuild: no work to do.\n";

    return 0;
}

void clean(
    const fs::path &build_dir)
{
    BuildPaths paths =
        make_paths(build_dir);

    FSEventsWatcher fsevents(paths.root);
    fsevents.start();

    std::error_code ec;

    fs::remove_all(
        paths.objects,
        ec);

}

void clobber(
    const fs::path &build_dir)
{
    BuildPaths paths =
        make_paths(build_dir);

    FSEventsWatcher fsevents(paths.root);
    fsevents.start();

    fs::path manifest =
        paths.root /
        "BUILD.nb";

    if (!file_exists(manifest))
        fail(
            "BUILD.nb not found: " +
            manifest.string());

    toml::Project project =
        toml::parse_file(
            manifest.string(),
            paths.build);

    std::error_code ec;

    /*
     * Remove the entire build directory.
     */
    fs::remove_all(
        paths.build,
        ec);

    /*
     * Every target owns its outputs now.
     */
    for (const auto &[name, target] :
         project.targets) {

        if (!target.output.empty()) {

            fs::remove_all(
                paths.root /
                target.output,
                ec);
        }

        for (const auto &output :
             target.outputs) {

            fs::remove_all(
                paths.root /
                output,
                ec);
        }
    }
}

void rebuild(
    const fs::path &build_dir,
    const std::vector<std::string> &targets)
{
    clean(build_dir);

    build(
        build_dir,
        targets,
        false);
}

void print_help()
{
    std::cout
        << "NightBuild - native C++ build system\n\n"

        << "Usage:\n"
        << "  nightbuild <command> [options] [targets...]\n\n"

        << "Commands:\n"
        << "  gen -C <dir>              Generate build files\n"
        << "  args -C <dir>             Apply build arguments and regenerate\n"
        << "  build -C <dir> [targets] Build selected targets\n"
        << "  rebuild -C <dir> [targets]\n"
        << "                            Clean and build selected targets\n"
        << "  clean -C <dir>            Remove build outputs\n"
        << "  clobber -C <dir>          Remove the entire build directory\n\n"

        << "Options:\n"
        << "  -C <dir>                  Use <dir> as the build directory\n"
        << "  -v, --verbose             Print commands as they execute\n"
        << "  --help                    Show this help message\n"
        << "  --version                 Show version information\n\n"

        << "Build arguments:\n"
        << "  args.nb                   Override declared build arguments\n"
        << "  declare_args(...)          Declare configurable build arguments\n"
        << "  Arguments are applied before build graph generation.\n\n"

        << "Targets:\n"
        << "  If no targets are specified, all default targets are built.\n"
        << "  Multiple targets may be specified.\n"
        << "  Target dependencies are built automatically.\n\n"

        << "Target types:\n"
        << "  executable                Native executable\n"
        << "  app                       macOS application bundle\n"
        << "  static_lib                Static library\n"
        << "  shared_lib                Shared library\n"
        << "  object                    Object target\n"
        << "  framework                 macOS framework\n"
        << "  generator                 Build-time generator\n"
        << "  action                    Custom build action\n\n"

        << "Build model:\n"
        << "  NightBuild generates a concrete command database.\n"
        << "  File and dependency hashes are used for incremental builds.\n"
        << "  Unchanged work is skipped automatically.\n\n"

        << "Toolchains:\n"
        << "  Toolchains are loaded during 'gen' using\n"
        << "  target.toolchain_include entries.\n\n"

        << "Examples:\n"
        << "  nightbuild gen -C out/Official\n"
        << "  nightbuild args -C out/Official\n"
        << "  nightbuild build -C out/Official\n"
        << "  nightbuild build -C out/Official nightbuild\n"
        << "  nightbuild build -C out/Official nightbuild test\n"
        << "  nightbuild build -C out/Official -v\n"
        << "  nightbuild rebuild -C out/Official\n"
        << "  nightbuild clean -C out/Official\n"
        << "  nightbuild clobber -C out/Official\n";
}

void print_version()
{
    std::cout
        << "NightBuild 0.2\n"
        << "Native C++ build system\n";
}

} // namespace

int main(
    int argc,
    char **argv)
{
    CLIResult cli =
        parse_cli(
            argc,
            argv);

    switch (cli.command) {

    case Command::Help:

        print_help();

        return 0;

    case Command::Version:

        print_version();

        return 0;

    case Command::Args: {
        fs::path build_dir =
            fs::absolute(
                cli.directory);
        fs::path root =
            fs::current_path();

        generate(
            root,
            build_dir);
        return 0;
    }

    case Command::Gen: {

        fs::path build_dir =
            fs::absolute(
                cli.directory);

        fs::path root = fs::current_path();

        generate(
            root,
            build_dir);

        return 0;
    }

    case Command::Build:

        // if (cli.tui)

        //     return build_tui(
        //         fs::absolute(
        //             cli.directory),
        //         build);

        return build(
            fs::absolute(
                cli.directory),
            cli.targets,
            cli.verbose);

    case Command::Rebuild:

        rebuild(
            fs::absolute(
                cli.directory),
            cli.targets);

        return 0;

    case Command::Clean:

        clean(
            fs::absolute(
                cli.directory));

        return 0;

    case Command::Clobber:

        clobber(
            fs::absolute(
                cli.directory));

        return 0;
    }

    return 0;
}//meow//meow//meow//meow//meow