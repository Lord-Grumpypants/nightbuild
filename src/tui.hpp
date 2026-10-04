#pragma once

#include <filesystem>

int build_tui(
    const std::filesystem::path &build_dir,
    int (*build_function)(
        const std::filesystem::path &,
        bool));