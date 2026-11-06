#pragma once
#include <filesystem>
#include <string>
#include <vector>
bool nightbuild_watcher_ping(std::string &root);
std::vector<std::filesystem::path> nightbuild_watcher_drain();
