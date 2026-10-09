#include "scheduler.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <utility>

namespace {

constexpr double UNKNOWN_COMMAND_BONUS = 1.20;
constexpr double SIZE_WEIGHT = 0.10;
constexpr double HISTORY_WEIGHT = 0.90;
constexpr double MIN_ESTIMATE = 0.001;

constexpr double COLD_START_BASE = 0.10;
constexpr double CONFIDENCE_K = 4.0;
constexpr double HARD_JOB_WEIGHT = 0.35;
constexpr double UNLOCK_WEIGHT = 0.75;
constexpr double EMA_ALPHA = 0.20;

constexpr std::uint64_t FNV_OFFSET =
    14695981039346656037ULL;

constexpr std::uint64_t FNV_PRIME =
    1099511628211ULL;

std::string mutable_string(
    const MutableCommandDatabase &database,
    std::uint64_t offset)
{
    if (database.header == nullptr ||
        database.strings == nullptr) {
        return {};
    }

    if (offset >= database.header->string_size)
        return {};

    const char *begin =
        database.strings + offset;

    const std::size_t remaining =
        database.header->string_size - offset;

    const void *terminator =
        std::memchr(begin, '\0', remaining);

    if (terminator == nullptr)
        return {};

    return std::string(
        begin,
        static_cast<const char *>(terminator) - begin);
}

std::uint64_t hash_string(
    const std::string &value)
{
    std::uint64_t hash = FNV_OFFSET;

    for (unsigned char c : value) {
        hash ^= c;
        hash *= FNV_PRIME;
    }

    return hash;
}

std::uint64_t hash_command(
    const MutableCommandDatabase &database,
    std::size_t command_index)
{
    if (database.header == nullptr ||
        database.commands == nullptr ||
        command_index >= database.header->command_count) {
        return 0;
    }

    const CommandRecord &command =
        database.commands[command_index];

    std::uint64_t hash = FNV_OFFSET;

    auto mix =
        [&](const void *data, std::size_t size) {

            const auto *bytes =
                static_cast<const unsigned char *>(data);

            for (std::size_t i = 0; i < size; ++i) {
                hash ^= bytes[i];
                hash *= FNV_PRIME;
            }
        };

    mix(
        &command.kind,
        sizeof(command.kind));

    mix(
        &command.flags,
        sizeof(command.flags));

    mix(
        &command.argc,
        sizeof(command.argc));

    if (database.arguments != nullptr) {

        for (std::uint64_t i = 0;
             i < command.argc;
             ++i) {

            const std::uint64_t offset =
                database.arguments[
                    command.argv_offset + i];

            const std::string argument =
                mutable_string(
                    database,
                    offset);

            mix(
                argument.data(),
                argument.size());

            const unsigned char zero = 0;
            mix(&zero, 1);
        }
    }

    return hash;
}

std::uint64_t source_size(
    const MutableCommandDatabase &database,
    std::size_t command_index)
{
    if (database.header == nullptr ||
        database.commands == nullptr ||
        command_index >= database.header->command_count) {
        return 0;
    }

    const CommandRecord &command =
        database.commands[command_index];

    const std::string source =
        mutable_string(
            database,
            command.source_path_offset);

    if (source.empty())
        return 0;

    struct stat st {};

    if (::stat(source.c_str(), &st) != 0)
        return 0;

    if (st.st_size < 0)
        return 0;

    return static_cast<std::uint64_t>(st.st_size);
}

std::uint64_t command_key(
    const MutableCommandDatabase &database,
    std::size_t command_index)
{
    return hash_command(
        database,
        command_index);
}

std::string telemetry_path(
    const BuildPaths &paths)
{
    return (
        paths.build /
        "telemetry").string();
}

} // namespace

Scheduler::Scheduler(
    const BuildPaths &paths,
    MutableCommandDatabase &database)
    : paths(paths),
      database(database)
{
    graph.resize(
        database.header != nullptr
            ? database.header->command_count
            : 0);

    build_graph();

    /*
     * Telemetry is intentionally loaded after the graph is
     * constructed. The graph itself is derived entirely from
     * the immutable command database.
     */
    std::ifstream input(
        telemetry_path(paths));

    if (!input)
        return;

    std::uint64_t key = 0;
    double average = 0.0;
    std::uint64_t samples = 0;

    while (input >> key >> average >> samples) {

        History history;

        history.average_duration =
            std::max(
                average,
                MIN_ESTIMATE);

        history.samples =
            samples;

        telemetry[key] =
            history;
    }
}

void Scheduler::build_graph()
{
    if (database.header == nullptr ||
        database.commands == nullptr) {
        return;
    }

    const std::size_t count =
        database.header->command_count;

    graph.clear();
    graph.resize(count);

    /*
     * Every build command has one output. Build a producer
     * lookup first so graph construction is O(commands *
     * arguments) rather than repeatedly scanning all commands.
     */
    std::unordered_map<
        std::string,
        std::size_t> producers;

    producers.reserve(
        count * 2);

    for (std::size_t i = 0;
         i < count;
         ++i) {

        const CommandRecord &command =
            database.commands[i];

        const std::string output =
            mutable_string(
                database,
                command.output_path_offset);

        if (output.empty())
            continue;

        producers.emplace(
            output,
            i);
    }

    /*
     * A command depends on another command when one of its
     * argv entries is exactly the output produced by that
     * command.
     *
     * This naturally handles:
     *
     *   object -> archive
     *   object -> executable
     *   archive -> executable
     *
     * without requiring another dependency representation in
     * the command database.
     */
    for (std::size_t consumer = 0;
         consumer < count;
         ++consumer) {

        const CommandRecord &command =
            database.commands[consumer];

        for (std::uint64_t i = 0;
             i < command.argc;
             ++i) {

            const std::uint64_t argument_offset =
                database.arguments[
                    command.argv_offset + i];

            const std::string argument =
                mutable_string(
                    database,
                    argument_offset);

            if (argument.empty())
                continue;

            const auto producer =
                producers.find(argument);

            if (producer == producers.end())
                continue;

            const std::size_t producer_index =
                producer->second;

            if (producer_index == consumer)
                continue;

            graph[consumer]
                .predecessors
                .push_back(producer_index);

            graph[producer_index]
                .successors
                .push_back(consumer);
        }
    }

    /*
     * An argument can theoretically reference the same output
     * more than once. Remove duplicate edges so dependency
     * counters remain correct.
     */
    for (Node &node : graph) {

        auto &predecessors =
            node.predecessors;

        std::sort(
            predecessors.begin(),
            predecessors.end());

        predecessors.erase(
            std::unique(
                predecessors.begin(),
                predecessors.end()),
            predecessors.end());

        auto &successors =
            node.successors;

        std::sort(
            successors.begin(),
            successors.end());

        successors.erase(
            std::unique(
                successors.begin(),
                successors.end()),
            successors.end());
    }
}

void Scheduler::add(
    std::size_t command_index)
{
    if (command_index >= graph.size())
        return;

    if (graph[command_index].requested)
        return;

    graph[command_index].requested = true;
    requested.insert(command_index);

    for (const std::size_t predecessor :
         graph[command_index].predecessors) {
        add(predecessor);
    }

    graph[command_index].remaining_dependencies = 0;

    for (const std::size_t predecessor :
         graph[command_index].predecessors) {
        if (predecessor >= graph.size())
            continue;

        if (!graph[predecessor].completed)
            ++graph[command_index].remaining_dependencies;
    }

    enqueue_if_ready(command_index);
}

void Scheduler::enqueue_if_ready(
    std::size_t command_index)
{
    if (command_index >= graph.size())
        return;

    Node &node =
        graph[command_index];

    if (!node.requested ||
        node.queued ||
        node.running ||
        node.completed) {
        return;
    }

    if (node.remaining_dependencies != 0)
        return;

    const double estimated =
        estimate(command_index);

    const double unlock =
        calculate_unlock_value(
            command_index);

    const double score =
        priority(
            command_index,
            estimated);

    Entry entry;

    entry.index =
        command_index;

    entry.score =
        score;

    entry.estimated_duration =
        estimated;

    entry.unlock_value =
        unlock;

    entry.source_size =
        source_size(
            database,
            command_index);

    const auto it =
        telemetry.find(
            command_key(
                database,
                command_index));

    entry.samples =
        it == telemetry.end()
            ? 0
            : it->second.samples;

    queue.push_back(
        std::move(entry));

    node.queued = true;
}

bool Scheduler::empty() const
{
    return queue.empty();
}

std::vector<std::size_t> Scheduler::ready() const
{
    std::vector<std::size_t> result;
    result.reserve(queue.size());

    for (const Entry &entry : queue)
        result.push_back(entry.index);

    return result;
}


bool Scheduler::done() const
{
    return completed_requested == requested.size();
}



std::size_t Scheduler::next()
{
    if (queue.empty())
        return std::numeric_limits<std::size_t>::max();

    const auto it =
        std::max_element(
            queue.begin(),
            queue.end(),
            [](const Entry &a,
               const Entry &b) {

                return a.score < b.score;
            });

    const std::size_t index =
        it->index;

    queue.erase(it);

    if (index < graph.size()) {

        graph[index].queued =
            false;

        graph[index].running =
            true;
    }

    return index;
}

void Scheduler::complete(
    std::size_t command_index)
{
    if (command_index >= graph.size())
        return;

    Node &node =
        graph[command_index];

    if (node.completed)
        return;

    node.running = false;
    node.completed = true;

    if (node.requested &&
        completed_requested < requested.size()) {

        ++completed_requested;
    }

    /*
     * Completing this command can make its dependents ready.
     */
    for (const std::size_t successor :
         node.successors) {

        if (successor >= graph.size())
            continue;

        Node &dependent =
            graph[successor];

        if (!dependent.requested ||
            dependent.completed) {
            continue;
        }

        if (dependent.remaining_dependencies > 0)
            --dependent.remaining_dependencies;

        enqueue_if_ready(
            successor);
    }
}

double Scheduler::estimate(
    std::size_t command_index) const
{
    if (command_index >= graph.size())
        return MIN_ESTIMATE;

    const std::uint64_t key =
        command_key(
            database,
            command_index);

    const auto it =
        telemetry.find(key);

    const std::uint64_t bytes =
        source_size(
            database,
            command_index);

    /*
     * We use the historical average whenever we have one.
     * Unknown commands receive a conservative estimate based
     * on source size.
     */
    if (it != telemetry.end() &&
        it->second.samples > 0) {

        const double history =
            std::max(
                it->second.average_duration,
                MIN_ESTIMATE);

        /*
         * The size component is deliberately weak. Telemetry
         * dominates once we have enough observations.
         */
        const double size_factor =
            1.0 +
            SIZE_WEIGHT *
            std::log2(
                1.0 +
                static_cast<double>(bytes) /
                    (64.0 * 1024.0));

        return std::max(
            history * size_factor,
            MIN_ESTIMATE);
    }

    const double size_mb =
        static_cast<double>(bytes) /
        (1024.0 * 1024.0);

    return std::max(
        COLD_START_BASE +
            UNKNOWN_COMMAND_BONUS *
            size_mb,
        MIN_ESTIMATE);
}

double Scheduler::calculate_unlock_value(
    std::size_t command_index) const
{
    if (command_index >= graph.size())
        return 0.0;

    const Node &node =
        graph[command_index];

    double value = 0.0;

    /*
     * Prefer commands which unlock expensive downstream work.
     */
    for (const std::size_t successor :
         node.successors) {

        if (successor >= graph.size())
            continue;

        const Node &dependent =
            graph[successor];

        if (!dependent.requested ||
            dependent.completed) {
            continue;
        }

        const double dependent_estimate =
            estimate(successor);

        const double fanout =
            static_cast<double>(
                dependent.successors.size());

        value +=
            dependent_estimate *
            (1.0 + HARD_JOB_WEIGHT * fanout);
    }

    return value;
}

double Scheduler::priority(
    std::size_t command_index,
    double estimated_duration) const
{
    if (command_index >= graph.size())
        return -std::numeric_limits<double>::infinity();

    const std::uint64_t key =
        command_key(
            database,
            command_index);

    const auto it =
        telemetry.find(key);

    double confidence = 0.0;

    std::uint64_t samples = 0;

    if (it != telemetry.end())
        samples =
            it->second.samples;

    if (samples != 0) {

        confidence =
            static_cast<double>(samples) /
            (
                static_cast<double>(samples) +
                CONFIDENCE_K
            );

        confidence =
            std::clamp(
                confidence,
                0.0,
                1.0);
    }

    const double unlock =
        calculate_unlock_value(
            command_index);

    const double duration_score =
        std::max(
            estimated_duration,
            MIN_ESTIMATE);

    /*
     * Large/long jobs get some preference because delaying them
     * can leave the final core of the build occupied by one
     * straggler.
     *
     * Unlock value pushes prerequisite work forward when it
     * opens substantial downstream work.
     */
    const double history_component =
        HISTORY_WEIGHT *
        duration_score;

    const double confidence_component =
        confidence *
        history_component;

    const double cold_component =
        (1.0 - confidence) *
        COLD_START_BASE;

    return
        confidence_component +
        cold_component +
        UNLOCK_WEIGHT * unlock;
}

void Scheduler::record(
    std::size_t command_index,
    double duration_seconds)
{
    if (command_index >= graph.size())
        return;

    duration_seconds =
        std::max(
            duration_seconds,
            MIN_ESTIMATE);

    const std::uint64_t key =
        command_key(
            database,
            command_index);

    History &history =
        telemetry[key];

    if (history.samples == 0) {

        history.average_duration =
            duration_seconds;

    } else {

        history.average_duration =
            (
                (1.0 - EMA_ALPHA) *
                history.average_duration
            ) +
            (
                EMA_ALPHA *
                duration_seconds
            );
    }

    ++history.samples;

    /*
     * Persist telemetry immediately so an interrupted build
     * still leaves useful history for the next invocation.
     */
    std::ofstream output(
        telemetry_path(paths),
        std::ios::trunc);

    if (!output)
        return;

    output.setf(
        std::ios::fixed);

    output.precision(6);

    for (const auto &[entry_key, entry] :
         telemetry) {

        output
            << entry_key
            << ' '
            << entry.average_duration
            << ' '
            << entry.samples
            << '\n';
    }
}