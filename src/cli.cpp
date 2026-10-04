#include "cli.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void usage_error(
    const std::string &message) {

    std::cerr
        << "nightbuild: "
        << message
        << '\n';

    std::cerr
        << "Try 'nightbuild --help' "
        << "for usage.\n";

    std::exit(EXIT_FAILURE);
}

Command parse_command(
    const std::string &command) {

    if (command == "gen")
        return Command::Gen;

    if (command == "build")
        return Command::Build;

    if (command == "rebuild")
        return Command::Rebuild;

    if (command == "clean")
        return Command::Clean;

    if (command == "clobber")
        return Command::Clobber;

    usage_error(
        "unknown command '" +
        command +
        "'");
}

} // namespace

CLIResult parse_cli(
    int argc,
    char **argv) {

    if (argc < 2) {

        return {
            Command::Help,
            ".",
            false,
            false,
            {}
        };
    }

    const std::string first =
        argv[1];

    if (first == "--help" ||
        first == "-h") {

        return {
            Command::Help,
            ".",
            false,
            false,
            {}
        };
    }

    if (first == "--version" ||
        first == "-V") {

        return {
            Command::Version,
            ".",
            false,
            false,
            {}
        };
    }

    CLIResult result;

    result.command =
        parse_command(first);

    /*
     * gen has no target selection.
     */
    if (result.command == Command::Gen) {

        bool found_directory = false;

        for (int i = 2;
             i < argc;
             ++i) {

            const std::string argument =
                argv[i];

            if (argument == "-v" ||
                argument == "--verbose") {

                result.verbose = true;

                continue;
            }

            if (argument == "--tui") {

                usage_error(
                    "--tui is only valid "
                    "with build commands");
            }

            if (argument == "-C") {

                if (found_directory) {

                    usage_error(
                        "multiple -C options");
                }

                if (i + 1 >= argc) {

                    usage_error(
                        "-C requires a directory");
                }

                result.directory =
                    argv[++i];

                found_directory = true;

                continue;
            }

            if (argument == "-h" ||
                argument == "--help") {

                return {
                    Command::Help,
                    ".",
                    result.verbose,
                    false,
                    {}
                };
            }

            usage_error(
                "unexpected argument '" +
                argument +
                "'");
        }

        /*
         * Preserve the existing:
         *
         *   nightbuild gen build
         *
         * form.
         */
        if (!found_directory) {

            if (argc == 3) {

                result.directory =
                    argv[2];

                return result;
            }

            usage_error(
                "gen requires a directory");
        }

        return result;
    }

    bool found_directory = false;

    for (int i = 2;
         i < argc;
         ++i) {

        const std::string argument =
            argv[i];

        if (argument == "-v" ||
            argument == "--verbose") {

            result.verbose = true;

            continue;
        }

        if (argument == "--tui") {

            result.tui = true;

            continue;
        }

        if (argument == "-C") {

            if (found_directory) {

                usage_error(
                    "multiple -C options");
            }

            if (i + 1 >= argc) {

                usage_error(
                    "-C requires a directory");
            }

            result.directory =
                argv[++i];

            found_directory = true;

            continue;
        }

        if (argument == "-h" ||
            argument == "--help") {

            return {
                Command::Help,
                ".",
                result.verbose,
                result.tui,
                {}
            };
        }

        /*
         * Anything else is a target name.
         *
         * Zero targets means all targets.
         */
        if (!argument.empty() &&
            argument[0] != '-') {

            result.targets.push_back(
                argument);

            continue;
        }

        usage_error(
            "unexpected argument '" +
            argument +
            "'");
    }

    if (!found_directory) {

        usage_error(
            "command '" +
            first +
            "' requires -C <dir>");
    }

    if (result.verbose &&
        result.tui) {

        usage_error(
            "--verbose and --tui "
            "cannot be used together");
    }

    return result;
}