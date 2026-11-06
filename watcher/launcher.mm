#include <mach-o/dyld.h>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace fs = std::filesystem;

namespace {
constexpr const char *service_label = "com.nightfall.NightBuildWatcher";

std::string xml_escape(const std::string &value) {
    std::string result;
    result.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&apos;"; break;
        default: result += ch; break;
        }
    }
    return result;
}

int run_launchctl(const std::vector<std::string> &arguments) {
    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (const std::string &argument : arguments)
        argv.push_back(const_cast<char *>(argument.c_str()));
    argv.push_back(nullptr);

    pid_t child = -1;
    const int spawn_error =
        posix_spawnp(&child, "launchctl", nullptr, nullptr, argv.data(), environ);
    if (spawn_error != 0) {
        errno = spawn_error;
        std::perror("nightbuild: launchctl");
        return -1;
    }

    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited < 0) {
        std::perror("nightbuild: waitpid");
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

fs::path executable_path(const char *argv0) {
    uint32_t capacity = 0;
    _NSGetExecutablePath(nullptr, &capacity);
    std::vector<char> buffer(capacity + 1, '\0');

    if (_NSGetExecutablePath(buffer.data(), &capacity) != 0)
        throw std::runtime_error("could not resolve the NightBuild executable path");

    std::error_code error;
    fs::path path = fs::canonical(buffer.data(), error);
    if (!error)
        return path;

    path = fs::absolute(argv0, error);
    if (error)
        throw std::runtime_error("could not resolve the NightBuild executable path");
    return path.lexically_normal();
}
} // namespace

int nightbuild_start_watcher_daemon(const char *argv0) {
    try {
        const fs::path nightbuild = executable_path(argv0);
        const fs::path watcher = nightbuild.parent_path() / "nightbuild-watcher";

        if (!fs::is_regular_file(watcher) || access(watcher.c_str(), X_OK) != 0) {
            std::cerr << "nightbuild: watcher executable not found or not executable: "
                      << watcher << "\nBuild the nightbuild-watcher target first.\n";
            return 1;
        }

        std::error_code error;
        const fs::path root = fs::canonical(fs::current_path(), error);
        if (error || !fs::is_directory(root)) {
            std::cerr << "nightbuild: could not resolve the current project directory\n";
            return 1;
        }

        const char *home = std::getenv("HOME");
        if (home == nullptr || *home == '\0') {
            std::cerr << "nightbuild: HOME is not set\n";
            return 1;
        }

        const fs::path agents = fs::path(home) / "Library" / "LaunchAgents";
        const fs::path plist = agents / (std::string(service_label) + ".plist");
        fs::create_directories(agents);

        const fs::path temporary = plist.string() + ".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            std::cerr << "nightbuild: could not write " << temporary << '\n';
            return 1;
        }

        output
            << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
               "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            << "<plist version=\"1.0\">\n<dict>\n"
            << "  <key>Label</key><string>" << service_label << "</string>\n"
            << "  <key>ProgramArguments</key><array>\n"
            << "    <string>" << xml_escape(watcher.string()) << "</string>\n"
            << "    <string>" << xml_escape(root.string()) << "</string>\n"
            << "  </array>\n"
            << "  <key>RunAtLoad</key><true/>\n"
            << "  <key>KeepAlive</key><true/>\n"
            << "  <key>ProcessType</key><string>Background</string>\n"
            << "  <key>MachServices</key><dict>\n"
            << "    <key>" << service_label << "</key><true/>\n"
            << "  </dict>\n</dict>\n</plist>\n";
        output.close();

        if (!output) {
            fs::remove(temporary);
            std::cerr << "nightbuild: failed writing LaunchAgent property list\n";
            return 1;
        }
        fs::rename(temporary, plist);

        const std::string domain = "gui/" + std::to_string(getuid());
        const std::string service = domain + "/" + service_label;
        (void)run_launchctl({"launchctl", "bootout", domain, plist.string()});

        if (run_launchctl({"launchctl", "bootstrap", domain, plist.string()}) != 0) {
            std::cerr << "nightbuild: launchctl could not bootstrap the watcher LaunchAgent\n";
            return 1;
        }
        if (run_launchctl({"launchctl", "kickstart", "-k", service}) != 0) {
            std::cerr << "nightbuild: launchctl could not start the watcher service\n";
            return 1;
        }

        std::cout << "NightBuild watcher started for " << root << '\n';
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << "nightbuild: " << exception.what() << '\n';
        return 1;
    }
}
