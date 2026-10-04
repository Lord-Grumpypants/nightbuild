#include "compile_commands.hpp"

#include "command.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

[[noreturn]] void fail_compile_commands(
    const std::string &message)
{
    throw std::runtime_error(
        "NightBuild: " + message);
}

std::string json_escape(
    const std::string &value)
{
    std::string result;
    result.reserve(value.size() + 16);

    for (unsigned char c : value) {
        switch (c) {
        case '"':
            result += "\\\"";
            break;

        case '\\':
            result += "\\\\";
            break;

        case '\b':
            result += "\\b";
            break;

        case '\f':
            result += "\\f";
            break;

        case '\n':
            result += "\\n";
            break;

        case '\r':
            result += "\\r";
            break;

        case '\t':
            result += "\\t";
            break;

        default:
            if (c < 0x20) {
                static constexpr char hex[] =
                    "0123456789abcdef";

                result += "\\u00";
                result += hex[(c >> 4) & 0x0f];
                result += hex[c & 0x0f];
            } else {
                result += static_cast<char>(c);
            }
            break;
        }
    }

    return result;
}

const char *database_string(
    const CommandFileHeader &header,
    const char *strings,
    std::uint64_t offset)
{
    if (offset >= header.string_size)
        fail_compile_commands(
            "invalid string offset");

    const char *value = strings + offset;

    const std::size_t remaining =
        header.string_size - offset;

    if (std::memchr(
            value,
            '\0',
            remaining) == nullptr)
        fail_compile_commands(
            "unterminated string in command database");

    return value;
}

void write_json_command(
    std::ofstream &output,
    const CommandFileHeader &header,
    const std::uint64_t *arguments,
    const char *strings,
    const CommandRecord &command)
{
    if (command.argv_offset > header.argument_count)
        fail_compile_commands(
            "invalid argv offset");

    if (command.argc >
        header.argument_count - command.argv_offset)
        fail_compile_commands(
            "invalid argv count");

    output << "\"command\":\"";

    for (std::uint64_t i = 0;
         i < command.argc;
         ++i) {
        if (i != 0)
            output << ' ';

        const std::uint64_t string_offset =
            arguments[
                command.argv_offset + i];

        const char *argument =
            database_string(
                header,
                strings,
                string_offset);

        output << json_escape(argument);
    }

    output << '"';
}

} // namespace

bool write_compile_commands(
    const BuildPaths &paths,
    const std::vector<std::string> &default_targets)
{
    const fs::path database_path =
        paths.commands;

    int fd = ::open(
        database_path.c_str(),
        O_RDONLY);

    if (fd < 0) {
        fail_compile_commands(
            "cannot open command database: " +
            database_path.string());
    }

    struct stat st {};

    if (fstat(fd, &st) != 0) {
        ::close(fd);

        fail_compile_commands(
            "cannot stat command database");
    }

    if (st.st_size <
        static_cast<off_t>(
            sizeof(CommandFileHeader))) {
        ::close(fd);

        fail_compile_commands(
            "command database is too small");
    }

    const std::size_t size =
        static_cast<std::size_t>(st.st_size);

    void *mapping = mmap(
        nullptr,
        size,
        PROT_READ,
        MAP_PRIVATE,
        fd,
        0);

    if (mapping == MAP_FAILED) {
        ::close(fd);

        fail_compile_commands(
            "mmap failed for command database");
    }

    auto cleanup = [&] {
        munmap(mapping, size);
        ::close(fd);
    };

    const auto *header =
        static_cast<const CommandFileHeader *>(
            mapping);

    if (std::memcmp(
            header->magic,
            COMMAND_MAGIC,
            sizeof(COMMAND_MAGIC)) != 0) {
        cleanup();

        fail_compile_commands(
            "invalid command database magic");
    }

    if (header->version != COMMAND_VERSION) {
        cleanup();

        fail_compile_commands(
            "unsupported command database version");
    }

    if (header->file_size != size) {
        cleanup();

        fail_compile_commands(
            "command database size mismatch");
    }

    const auto *commands =
        reinterpret_cast<const CommandRecord *>(
            static_cast<const std::uint8_t *>(mapping) +
            header->command_offset);

    const auto *arguments =
        reinterpret_cast<const std::uint64_t *>(
            static_cast<const std::uint8_t *>(mapping) +
            header->argument_offset);

    const char *strings =
        reinterpret_cast<const char *>(
            static_cast<const std::uint8_t *>(mapping) +
            header->string_offset);

    const fs::path output_path =
        paths.root / "compile_commands.json";

    std::ofstream output(
        output_path,
        std::ios::binary |
        std::ios::trunc);

    if (!output) {
        cleanup();

        fail_compile_commands(
            "cannot create " +
            output_path.string());
    }

    std::unordered_set<std::string> selected_targets(
        default_targets.begin(),
        default_targets.end());

    output << "[\n";

    bool first = true;

    for (std::uint64_t i = 0;
         i < header->command_count;
         ++i) {

        const CommandRecord &command =
            commands[i];

        if (static_cast<CommandKind>(
                command.kind) !=
            CommandKind::Compile)
            continue;

        const char *target_name =
            database_string(
                *header,
                strings,
                command.target_name_offset);

        if (!selected_targets.contains(target_name))
            continue;

        if (!first)
            output << ",\n";

        first = false;

        const char *source =
            database_string(
                *header,
                strings,
                command.source_path_offset);

        output << "  {\n";

        output << "    \"directory\":\""
               << json_escape(
                      paths.build.string())
               << "\",\n";

        output << "    ";

        write_json_command(
            output,
            *header,
            arguments,
            strings,
            command);

        output << ",\n";

        output << "    \"file\":\""
               << json_escape(source)
               << "\"\n";

        output << "  }";
    }

    output << "\n]\n";

    if (!output) {
        cleanup();

        fail_compile_commands(
            "failed writing " +
            output_path.string());
    }

    output.close();

    cleanup();

    return true;
}