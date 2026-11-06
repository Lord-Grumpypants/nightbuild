#include <xpc/xpc.h>

#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>

namespace {
constexpr const char *service_name = "com.nightfall.NightBuildWatcher";

std::string json_escape(const char *value) {
    std::string result;
    for (const unsigned char ch : std::string_view(value == nullptr ? "" : value)) {
        switch (ch) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (ch < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                result += "\\u00";
                result += hex[ch >> 4];
                result += hex[ch & 0x0f];
            } else {
                result += static_cast<char>(ch);
            }
        }
    }
    return result;
}

xpc_object_t send_command(const char *command) {
    xpc_connection_t connection =
        xpc_connection_create_mach_service(service_name, nullptr, 0);
    if (connection == nullptr) {
        std::cerr << "nightbuild-watchctl: could not create XPC connection\n";
        return nullptr;
    }

    xpc_connection_set_event_handler(connection, ^(xpc_object_t) {});
    xpc_connection_resume(connection);

    xpc_object_t message = xpc_dictionary_create(nullptr, nullptr, 0);
    xpc_dictionary_set_string(message, "command", command);
    xpc_object_t reply =
        xpc_connection_send_message_with_reply_sync(connection, message);

    xpc_release(message);
    xpc_connection_cancel(connection);
    xpc_release(connection);

    if (reply == nullptr) {
        std::cerr << "nightbuild-watchctl: watcher returned no reply\n";
        return nullptr;
    }
    if (xpc_get_type(reply) == XPC_TYPE_ERROR) {
        std::cerr << "nightbuild-watchctl: XPC request failed (watcher unavailable)\n";
        xpc_release(reply);
        return nullptr;
    }

    const char *error = xpc_dictionary_get_string(reply, "error");
    if (error != nullptr) {
        std::cerr << "nightbuild-watchctl: " << error << '\n';
        xpc_release(reply);
        return nullptr;
    }
    return reply;
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 2 ||
        (std::string_view(argv[1]) != "ping" &&
         std::string_view(argv[1]) != "drain")) {
        std::fprintf(stderr,
            "usage: nightbuild-watchctl <ping|drain>\n"
            "  ping   Check whether the watcher is responding\n"
            "  drain  Atomically retrieve and clear pending changed paths\n");
        return 2;
    }

    const bool drain = std::string_view(argv[1]) == "drain";
    xpc_object_t reply = send_command(drain ? "take_dirty" : "ping");
    if (reply == nullptr)
        return 1;

    if (!drain) {
        const bool running = xpc_dictionary_get_bool(reply, "running");
        const char *root = xpc_dictionary_get_string(reply, "root");
        std::cout << "{\\"running\\":" << (running ? "true" : "false");
        if (root != nullptr)
            std::cout << ",\\"root\\":\\"" << json_escape(root) << "\\"";
        std::cout << "}\n";
        xpc_release(reply);
        return running ? 0 : 1;
    }

    xpc_object_t paths = xpc_dictionary_get_value(reply, "paths");
    if (paths == nullptr || xpc_get_type(paths) != XPC_TYPE_ARRAY) {
        std::cerr << "nightbuild-watchctl: malformed dirty-path reply\n";
        xpc_release(reply);
        return 1;
    }

    std::cout << '[';
    const size_t count = xpc_array_get_count(paths);
    for (size_t i = 0; i < count; ++i) {
        xpc_object_t value = xpc_array_get_value(paths, i);
        const char *path = xpc_string_get_string_ptr(value);
        if (path == nullptr)
            continue;
        if (i != 0)
            std::cout << ',';
        std::cout << '\\"' << json_escape(path) << '\\"';
    }
    std::cout << "]\n";
    xpc_release(reply);
    return 0;
}
