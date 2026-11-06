#include "xpc.hpp"
#include <xpc/xpc.h>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
constexpr const char *service_name = "com.nightfall.NightBuildWatcher";
xpc_object_t send_command(const char *command) {
    xpc_connection_t connection = xpc_connection_create_mach_service(service_name, nullptr, 0);
    if (!connection) throw std::runtime_error("could not create watcher XPC connection");
    xpc_connection_set_event_handler(connection, ^(xpc_object_t) {});
    xpc_connection_resume(connection);
    xpc_object_t message = xpc_dictionary_create(nullptr, nullptr, 0);
    xpc_dictionary_set_string(message, "command", command);
    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(connection, message);
    xpc_release(message); xpc_connection_cancel(connection); xpc_release(connection);
    if (!reply) throw std::runtime_error("watcher returned no XPC reply");
    if (xpc_get_type(reply) == XPC_TYPE_ERROR) { xpc_release(reply); throw std::runtime_error("watcher XPC service is unavailable"); }
    const char *error = xpc_dictionary_get_string(reply, "error");
    if (error) { std::string detail(error); xpc_release(reply); throw std::runtime_error(detail); }
    return reply;
}
}
bool nightbuild_watcher_ping(std::string &root) {
    try { xpc_object_t reply=send_command("ping"); bool running=xpc_dictionary_get_bool(reply,"running"); const char *value=xpc_dictionary_get_string(reply,"root"); root=value?value:""; xpc_release(reply); return running; }
    catch (...) { return false; }
}
std::vector<std::filesystem::path> nightbuild_watcher_drain() {
    xpc_object_t reply=send_command("take_dirty"); xpc_object_t paths=xpc_dictionary_get_value(reply,"paths");
    if (!paths || xpc_get_type(paths)!=XPC_TYPE_ARRAY) { xpc_release(reply); throw std::runtime_error("watcher returned malformed dirty paths"); }
    std::vector<std::filesystem::path> result; size_t count=xpc_array_get_count(paths); result.reserve(count);
    for(size_t i=0;i<count;++i){ xpc_object_t value=xpc_array_get_value(paths,i); const char *path=xpc_string_get_string_ptr(value); if(!path){xpc_release(reply);throw std::runtime_error("watcher returned a non-string dirty path");} result.emplace_back(path); }
    xpc_release(reply); return result;
}
