#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

namespace fs = std::filesystem;

static constexpr char COMMAND_MAGIC[8] = {
    'N', 'B', 'C', 'M', 'D', 'B', '\0', '\0'
};

static constexpr std::uint32_t COMMAND_VERSION = 3;

#pragma pack(push, 1)

struct CommandFileHeader {
    char magic[8];

    std::uint32_t version;
    std::uint32_t flags;

    std::uint64_t file_size;

    std::uint64_t command_count;
    std::uint64_t command_offset;

    std::uint64_t argument_count;
    std::uint64_t argument_offset;

    std::uint64_t hash_count;
    std::uint64_t hash_offset;

    std::uint64_t string_offset;
    std::uint64_t string_size;

    std::uint64_t manifest_hash_offset;
};

struct HashRecord {
    std::int64_t mtime_seconds;
    std::int32_t mtime_nanoseconds;
    std::uint8_t digest[32];
};

#pragma pack(pop)

struct BuildPaths {
    fs::path root;
    fs::path build;
    fs::path objects;
    fs::path state;
    fs::path commands;
};

struct CommandRecord {
    std::uint64_t target_name_offset;

    std::uint32_t kind;
    std::uint32_t flags;

    std::uint64_t argc;
    std::uint64_t argv_offset;

    std::uint64_t description_offset;

    std::uint64_t source_path_offset;
    std::uint64_t output_path_offset;
    std::uint64_t dependency_path_offset;

    std::uint64_t source_hash_index;
    std::uint64_t dependency_hash_index;
    std::uint64_t input_hash_index;

    std::uint64_t state_hash_index;
};

struct MutableCommandDatabase {
    int fd = -1;
    std::size_t size = 0;
    void *mapping = MAP_FAILED;

    CommandFileHeader *header = nullptr;
    CommandRecord *commands = nullptr;
    std::uint64_t *arguments = nullptr;
    HashRecord *hashes = nullptr;
    char *strings = nullptr;

    MutableCommandDatabase() = default;

    MutableCommandDatabase(
        const MutableCommandDatabase &) = delete;

    MutableCommandDatabase &operator=(
        const MutableCommandDatabase &) = delete;

    MutableCommandDatabase(
        MutableCommandDatabase &&other) noexcept
        : fd(other.fd),
          size(other.size),
          mapping(other.mapping),
          header(other.header),
          commands(other.commands),
          arguments(other.arguments),
          hashes(other.hashes),
          strings(other.strings)
    {
        other.fd = -1;
        other.size = 0;
        other.mapping = MAP_FAILED;
        other.header = nullptr;
        other.commands = nullptr;
        other.arguments = nullptr;
        other.hashes = nullptr;
        other.strings = nullptr;
    }

    MutableCommandDatabase &operator=(
        MutableCommandDatabase &&other) noexcept
    {
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

    ~MutableCommandDatabase()
    {
        close();
    }

    void close()
    {
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

class Scheduler {

public:

    Scheduler(
        const BuildPaths &paths,
        MutableCommandDatabase &database);

    void add(
        std::size_t command_index);

    bool empty() const;

    bool done() const;

    std::size_t next();

    double estimate(
        std::size_t command_index) const;

    double priority(
        std::size_t command_index,
        double estimated_duration) const;

    void record(
        std::size_t command_index,
        double duration_seconds);

    void complete(
        std::size_t command_index);

private:

    struct History {
        double average_duration = 0.0;
        std::uint64_t samples = 0;
    };

    struct Entry {
        std::size_t index;

        double score;
        double estimated_duration;
        double unlock_value;

        std::uint64_t source_size;
        std::uint64_t samples;
    };

    using Telemetry =
        std::unordered_map<
            std::uint64_t,
            History>;

    const BuildPaths &paths;

    MutableCommandDatabase &database;

    Telemetry telemetry;

    std::vector<Entry> queue;

    // Scheduler graph/state goes here.
    struct Node {
        std::vector<std::size_t> predecessors;
        std::vector<std::size_t> successors;

        std::size_t remaining_dependencies = 0;

        bool requested = false;
        bool queued = false;
        bool running = false;
        bool completed = false;
    };

    std::vector<Node> graph;

    std::unordered_set<std::size_t> requested;

    std::size_t completed_requested = 0;

    void build_graph();

    void enqueue_if_ready(
        std::size_t command_index);
        
    double calculate_unlock_value(
        std::size_t command_index) const;
};