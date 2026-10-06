
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace toml {

struct Target {

    std::string name;

    std::string type;

    std::string output;

    std::vector<std::string> sources;

    std::vector<std::string> dependencies;
    std::vector<std::string> generator_deps;

    std::vector<std::string> include_dirs;

    /**
     * Legacy C/C++ flags member.
     *
     * Kept synchronized with flags["cxxflags"].
     */
    std::vector<std::string> cxxflags;

    /**
     * Linker flags.
     */
    std::vector<std::string> ldflags;

    /**
     * Generic toolchain flags.
     *
     * Example:
     *
     *   [target.app.flags]
     *   cxxflags = ["-O3", "-std=c++23"]
     *   objcxxflags = ["-fobjc-arc"]
     *   rustflags = ["-C", "opt-level=3"]
     */
    std::unordered_map<
        std::string,
        std::vector<std::string>>
        flags;

    /**
     * Generation-time JSON toolchain files.
     */
    std::vector<std::string> toolchain_include;

    std::vector<std::string> pkg_config;

    std::vector<std::string> library_dirs;

    std::vector<std::string> libraries;

    std::vector<std::string> frameworks;

    /**
     * ACTION target configuration.
     */
    std::vector<std::string> action;

    std::vector<std::string> inputs;

    std::vector<std::string> outputs;

    /**
     * Commands that must run before compilation.
     */
    std::vector<std::vector<std::string>>
        pre_actions;
};

struct Project {

    /**
     * Project-wide name.
     */
    std::string project;

    /**
     * Targets built by default and indexed by compile_commands.json.
     */
    std::vector<std::string> default_targets;

    /**
     * SHA-256 fingerprint of the complete TOML configuration tree.
     *
     * This includes the root manifest and every recursively included
     * manifest, as well as the include relationships between them.
     *
     * It is used by generation/incremental-generation logic to ensure
     * that changing an included manifest invalidates the generated
     * command database.
     */
    std::string configuration_hash;

    /**
     * Named build targets.
     *
     * Example:
     *
     *   project.targets["app"]
     *   project.targets["tests"]
     */
    std::unordered_map<
        std::string,
        Target>
        targets;
};

Project parse_file(
    const std::string &path,
    const std::filesystem::path &build_dir);

} // namespace toml