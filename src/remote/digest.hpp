#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace nightbuild::remote {

struct Digest {
    std::string hash;
    std::int64_t size_bytes = 0;

    bool operator==(const Digest &) const = default;
};

// REAPI-compatible SHA-256 digest: lowercase hexadecimal hash and byte size.
Digest sha256(std::span<const std::byte> bytes);

} // namespace nightbuild::remote
