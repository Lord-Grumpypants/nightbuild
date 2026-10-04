#pragma once

#include "scheduler.hpp"

#include <string>
#include <vector>

bool write_compile_commands(
    const BuildPaths &paths,
    const std::vector<std::string> &default_targets);