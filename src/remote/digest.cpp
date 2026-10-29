#include "digest.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <array>
#include <limits>
#include <stdexcept>

namespace nightbuild::remote {

Digest sha256(std::span<const std::byte> bytes)
{
    if (bytes.size() >
        static_cast<std::size_t>(
            std::numeric_limits<std::int64_t>::max())) {
        throw std::length_error("remote digest input is too large");
    }

    CC_SHA256_CTX context;
    if (CC_SHA256_Init(&context) != 1)
        throw std::runtime_error("CC_SHA256_Init failed");

    // CC_SHA256_Update accepts CC_LONG lengths, so process large inputs in
    // bounded chunks rather than truncating a size_t to CC_LONG.
    constexpr std::size_t chunk_limit =
        static_cast<std::size_t>(
            std::numeric_limits<CC_LONG>::max());

    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        const auto chunk_size = static_cast<CC_LONG>(
            remaining < chunk_limit ? remaining : chunk_limit);

        if (CC_SHA256_Update(
                &context,
                bytes.data() + offset,
                chunk_size) != 1) {
            throw std::runtime_error("CC_SHA256_Update failed");
        }

        offset += chunk_size;
    }

    std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> result{};
    if (CC_SHA256_Final(result.data(), &context) != 1)
        throw std::runtime_error("CC_SHA256_Final failed");

    constexpr char hex[] = "0123456789abcdef";
    std::string hash;
    hash.resize(result.size() * 2);

    for (std::size_t i = 0; i < result.size(); ++i) {
        hash[i * 2] = hex[result[i] >> 4];
        hash[i * 2 + 1] = hex[result[i] & 0x0f];
    }

    return {std::move(hash), static_cast<std::int64_t>(bytes.size())};
}

} // namespace nightbuild::remote
