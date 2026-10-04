#pragma once

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

class FSEventsWatcher {
public:
    explicit FSEventsWatcher(const fs::path& root);
    ~FSEventsWatcher();

    FSEventsWatcher(const FSEventsWatcher&) = delete;
    FSEventsWatcher& operator=(const FSEventsWatcher&) = delete;

    void start();
    void stop();

    std::vector<fs::path> drain();

private:
    struct Impl;
    Impl* impl_;
};