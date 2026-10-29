#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nightbuild::remote {

struct ServerCapabilities {
    bool cache_enabled = false;
    bool execution_enabled = false;
    std::vector<std::int32_t> digest_functions;
};

// Performs the official REAPI Capabilities.GetCapabilities RPC.
// The endpoint is host:port. TLS is used unless insecure is explicitly true.
ServerCapabilities get_capabilities(
    const std::string &endpoint,
    bool insecure = false);

} // namespace nightbuild::remote
