#include "fsevents.hpp"

#include <CoreServices/CoreServices.h>

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace {

struct StreamContext {
    std::mutex mutex;
    std::unordered_set<std::string> dirty_paths;
};

void fsevents_callback(
    ConstFSEventStreamRef,
    void* client_callback_info,
    size_t num_events,
    void* event_paths,
    const FSEventStreamEventFlags event_flags[],
    const FSEventStreamEventId[]) {

    auto* context =
        static_cast<StreamContext*>(client_callback_info);

    const auto* paths =
        static_cast<const char* const*>(event_paths);

    std::lock_guard lock(context->mutex);

    for (size_t i = 0; i < num_events; ++i) {
        if ((event_flags[i] & kFSEventStreamEventFlagUserDropped) ||
            (event_flags[i] & kFSEventStreamEventFlagKernelDropped)) {
            continue;
        }

        if (paths[i] != nullptr)
            context->dirty_paths.emplace(paths[i]);
    }
}

} // namespace

struct FSEventsWatcher::Impl {
    fs::path root;
    StreamContext context;
    FSEventStreamRef stream = nullptr;
    bool running = false;

    explicit Impl(fs::path root_path)
        : root(std::move(root_path)) {}
};

FSEventsWatcher::FSEventsWatcher(const fs::path& root)
    : impl_(new Impl(fs::absolute(root).lexically_normal())) {}

FSEventsWatcher::~FSEventsWatcher() {
    stop();
    delete impl_;
}

void FSEventsWatcher::start() {
    if (impl_->running)
        return;

    CFStringRef path =
        CFStringCreateWithCString(
            nullptr,
            impl_->root.c_str(),
            kCFStringEncodingUTF8);

    if (!path)
        throw std::runtime_error(
            "failed to create FSEvents root path");

    CFArrayRef paths =
        CFArrayCreate(
            nullptr,
            reinterpret_cast<const void**>(&path),
            1,
            &kCFTypeArrayCallBacks);

    CFRelease(path);

    if (!paths)
        throw std::runtime_error(
            "failed to create FSEvents path array");

    FSEventStreamContext context = {};
    context.version = 0;
    context.info = &impl_->context;

    const FSEventStreamCreateFlags flags =
        kFSEventStreamCreateFlagFileEvents |
        kFSEventStreamCreateFlagNoDefer;

    impl_->stream =
        FSEventStreamCreate(
            nullptr,
            &fsevents_callback,
            &context,
            paths,
            kFSEventStreamEventIdSinceNow,
            0.05,
            flags);

    CFRelease(paths);

    if (!impl_->stream)
        throw std::runtime_error(
            "failed to create FSEventStream");

    FSEventStreamSetDispatchQueue(
        impl_->stream,
        dispatch_get_global_queue(
            QOS_CLASS_UTILITY,
            0));

    if (!FSEventStreamStart(impl_->stream)) {
        FSEventStreamInvalidate(impl_->stream);
        FSEventStreamRelease(impl_->stream);
        impl_->stream = nullptr;

        throw std::runtime_error(
            "failed to start FSEventStream");
    }

    impl_->running = true;
}

void FSEventsWatcher::stop() {
    if (!impl_->stream)
        return;

    FSEventStreamStop(impl_->stream);
    FSEventStreamInvalidate(impl_->stream);
    FSEventStreamRelease(impl_->stream);

    impl_->stream = nullptr;
    impl_->running = false;
}

std::vector<fs::path> FSEventsWatcher::drain() {
    std::vector<fs::path> result;

    std::lock_guard lock(impl_->context.mutex);

    result.reserve(
        impl_->context.dirty_paths.size());

    for (const std::string& path :
         impl_->context.dirty_paths) {

        fs::path absolute =
            fs::path(path).lexically_normal();

        std::error_code ec;

        fs::path relative =
            fs::relative(
                absolute,
                impl_->root,
                ec);

        if (ec)
            result.emplace_back(std::move(absolute));
        else
            result.emplace_back(std::move(relative));
    }

    impl_->context.dirty_paths.clear();

    std::sort(
        result.begin(),
        result.end());

    return result;
}