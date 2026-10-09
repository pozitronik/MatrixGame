// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdlib>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace tests {

#if defined(_MSC_VER) && defined(_DEBUG)
void configure_crt_reports();
#endif

struct Case {
    const char *name;
    void (*run)();
};

inline void require(bool condition, const char *expression, const char *file, int line) {
    if (!condition) {
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + ": " + expression);
    }
}

inline int run(int argc, char **argv, std::span<const Case> cases) {
#if defined(_MSC_VER) && defined(_DEBUG)
    configure_crt_reports();
#endif
    if (argc == 2 && std::string_view(argv[1]) == "--list") {
        for (const auto &test : cases) {
            std::cout << test.name << '\n';
        }
        return EXIT_SUCCESS;
    }
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <test-name> | --list\n";
        return 2;
    }
    for (const auto &test : cases) {
        if (std::string_view(argv[1]) != test.name) {
            continue;
        }
        try {
            test.run();
            std::cout << "Passed: " << test.name << '\n';
            return EXIT_SUCCESS;
        }
        catch (const std::exception &error) {
            std::cerr << "Failed: " << test.name << "\n" << error.what() << '\n';
        }
        catch (...) {
            std::cerr << "Failed: " << test.name << "\nUnexpected exception\n";
        }
        return EXIT_FAILURE;
    }
    std::cerr << "Unknown test: " << argv[1] << "\nUse --list to see available tests.\n";
    return 2;
}

}  // namespace tests

// Unlike assert(), these checks remain active in Release builds.
#define MG_CHECK(expression) ::tests::require(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
