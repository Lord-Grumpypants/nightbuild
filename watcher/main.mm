#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <xpc/xpc.h>

#include <algorithm>
#include <cstdio>
#include <vector>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {
constexpr const char *service_name = "com.nightfall.NightBuildWatcher";

struct WatcherState {
    fs::path root;
    std::mutex mutex;
    std::unordered_set<std::string> dirty_paths;
    FSEventStreamRef stream = nullptr;
};

void fsevents_callback(
    ConstFSEventStreamRef,
    void *context,
    size_t count,
    void *event_paths,
    const FSEventStreamEventFlags flags[],
    const FSEventStreamEventId[]) {

    auto *state = static_cast<WatcherState *>(context);
    const auto *paths = static_cast<const char *const *>(event_paths);
    std::lock_guard lock(state->mutex);

    for (size_t i = 0; i < count; ++i) {
        if (flags[i] & (kFSEventStreamEventFlagUserDropped |
                        kFSEventStreamEventFlagKernelDropped |
                        kFSEventStreamEventFlagMustScanSubDirs))
            state->dirty_paths.insert(state->root.string());

        if (paths[i] != nullptr)
            state->dirty_paths.emplace(paths[i]);
    }
}

void send_reply(xpc_connection_t connection, xpc_object_t message) {
    xpc_object_t reply = xpc_dictionary_create_reply(message);
    if (reply == nullptr)
        return;

    const char *command = xpc_dictionary_get_string(message, "command");
    if (command == nullptr) {
        xpc_dictionary_set_string(reply, "error", "missing command");
    } else if (std::string(command) == "ping") {
        xpc_dictionary_set_bool(reply, "running", true);
        auto *state = static_cast<WatcherState *>(
            xpc_connection_get_context(connection));
        if (state != nullptr)
            xpc_dictionary_set_string(reply, "root", state->root.c_str());
    } else if (std::string(command) == "take_dirty") {
        auto *state = static_cast<WatcherState *>(
            xpc_connection_get_context(connection));
        xpc_object_t paths = xpc_array_create(nullptr, 0);

        if (state != nullptr) {
            std::vector<std::string> snapshot;
            {
                std::lock_guard lock(state->mutex);
                snapshot.reserve(state->dirty_paths.size());
                for (const std::string &path : state->dirty_paths)
                    snapshot.push_back(path);
                state->dirty_paths.clear();
            }

            std::sort(snapshot.begin(), snapshot.end());
            for (const std::string &path : snapshot) {
                xpc_object_t value = xpc_string_create(path.c_str());
                xpc_array_append_value(paths, value);
                xpc_release(value);
            }
        }

        xpc_dictionary_set_value(reply, "paths", paths);
        xpc_dictionary_set_uint64(reply, "count", xpc_array_get_count(paths));
        xpc_release(paths);
    } else {
        xpc_dictionary_set_string(reply, "error", "unknown command");
    }

    xpc_connection_send_message(connection, reply);
    xpc_release(reply);
}

void start_xpc_service(WatcherState &state) {
    xpc_connection_t listener = xpc_connection_create_mach_service(
        service_name, dispatch_get_main_queue(),
        XPC_CONNECTION_MACH_SERVICE_LISTENER);
    if (listener == nullptr)
        throw std::runtime_error("could not create the XPC listener");

    xpc_connection_set_event_handler(listener, ^(xpc_object_t peer) {
        if (xpc_get_type(peer) != XPC_TYPE_CONNECTION)
            return;

        xpc_connection_set_context((xpc_connection_t)peer, &state);
        xpc_connection_set_event_handler((xpc_connection_t)peer, ^(xpc_object_t event) {
            if (xpc_get_type(event) == XPC_TYPE_DICTIONARY)
                send_reply((xpc_connection_t)peer, event);
        });
        xpc_connection_resume((xpc_connection_t)peer);
    });
    xpc_connection_resume(listener);
}

void start_fsevents(WatcherState &state) {
    CFStringRef root = CFStringCreateWithCString(
        nullptr, state.root.c_str(), kCFStringEncodingUTF8);
    if (root == nullptr)
        throw std::runtime_error("could not create the watched root path");

    const void *values[] = {root};
    CFArrayRef paths = CFArrayCreate(nullptr, values, 1, &kCFTypeArrayCallBacks);
    CFRelease(root);
    if (paths == nullptr)
        throw std::runtime_error("could not create the FSEvents path list");

    FSEventStreamContext context = {};
    context.version = 0;
    context.info = &state;
    const FSEventStreamCreateFlags stream_flags =
        kFSEventStreamCreateFlagFileEvents |
        kFSEventStreamCreateFlagNoDefer;

    state.stream = FSEventStreamCreate(
        nullptr, &fsevents_callback, &context, paths,
        kFSEventStreamEventIdSinceNow, 0.05, stream_flags);
    CFRelease(paths);
    if (state.stream == nullptr)
        throw std::runtime_error("could not create the FSEvents stream");

    FSEventStreamSetDispatchQueue(
        state.stream, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
    if (!FSEventStreamStart(state.stream)) {
        FSEventStreamInvalidate(state.stream);
        FSEventStreamRelease(state.stream);
        state.stream = nullptr;
        throw std::runtime_error("could not start the FSEvents stream");
    }
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: nightbuild-watcher <project-root>\n");
        return 2;
    }

    try {
        std::error_code error;
        const fs::path root = fs::canonical(argv[1], error);
        if (error || !fs::is_directory(root)) {
            std::fprintf(stderr, "nightbuild-watcher: invalid project root\n");
            return 1;
        }

        WatcherState state;
        state.root = root;
        start_fsevents(state);
        start_xpc_service(state);
        dispatch_main();
    } catch (const std::exception &exception) {
        std::fprintf(stderr, "nightbuild-watcher: %s\n", exception.what());
        return 1;
    }
    return 0;
}
