#include "digest.hpp"

#include <cstddef>
#include <iostream>
#include <span>
#include <string_view>

using nightbuild::remote::sha256;

int main()
{
    const auto digest = [](std::string_view input) {
        return sha256(std::as_bytes(
            std::span<const char>(input.data(), input.size())));
    };

    const auto empty = digest("");
    if (empty.hash != "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" ||
        empty.size_bytes != 0) {
        std::cerr << "SHA-256 empty-input test: FAIL\n";
        return 1;
    }

    const auto abc = digest("abc");
    if (abc.hash != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" ||
        abc.size_bytes != 3) {
        std::cerr << "SHA-256 abc test: FAIL\n";
        return 1;
    }

    std::cout << "NightBuild REAPI digest tests: PASS\n";
    return 0;
}
