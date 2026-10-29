#include "client.hpp"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        std::cerr
            << "Usage: remote-capabilities <host:port> [--insecure]\n";
        return 2;
    }

    const bool insecure = argc == 3 && std::string(argv[2]) == "--insecure";
    if (argc == 3 && !insecure) {
        std::cerr << "error: the only optional argument is --insecure\n";
        return 2;
    }

    try {
        const auto capabilities =
            nightbuild::remote::get_capabilities(argv[1], insecure);

        std::cout << "REAPI server: " << argv[1] << '\n'
                  << "Action cache: "
                  << (capabilities.cache_enabled ? "available" : "unavailable")
                  << '\n'
                  << "Execution: "
                  << (capabilities.execution_enabled ? "enabled" : "disabled")
                  << '\n'
                  << "Digest functions:";
        for (const auto function : capabilities.digest_functions)
            std::cout << ' ' << function;
        std::cout << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "remote-capabilities: " << error.what() << '\n';
        return 1;
    }
}
