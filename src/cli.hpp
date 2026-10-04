#pragma once

#include <string>
#include <vector>

enum class Command {
    Gen,
    Build,
    Rebuild,
    Clean,
    Clobber,
    Help,
    Version,
};

struct CLIResult {

    Command command = Command::Help;

    std::string directory = ".";

    bool verbose = false;

    bool tui = false;

    /*
     * Empty means all targets.
     */
    std::vector<std::string> targets;
};

CLIResult parse_cli(int argc, char **argv);