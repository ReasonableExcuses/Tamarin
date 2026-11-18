// ============================================================================
// test_main.cpp - Tamarin test runner
// ============================================================================

#include "test_harness.h"
#include <iostream>

int main(int argc, char* argv[]) {
    std::string filter;
    if (argc > 1) filter = argv[1];

    int passed = 0;
    int failed = 0;

    std::cout << "=== Running Tamarin Compiler Tests ===\n\n" << std::flush;

    for (const auto& test : getRegisteredTests()) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) continue;
        std::cout << "[ RUN      ] " << test.name << std::endl;
        bool ok = false;
        try {
            ok = test.fn();
        } catch (const std::exception& e) {
            std::cerr << "EXCEPTION: " << e.what() << "\n";
            ok = false;
        }

        if (ok) {
            std::cout << "[       OK ] " << test.name << std::endl;
            ++passed;
        } else {
            std::cout << "[  FAILED  ] " << test.name << std::endl;
            ++failed;
        }
    }

    std::cout << "\n=========================================\n"
              << "Total: " << (passed + failed)
              << " | Passed: " << passed
              << " | Failed: " << failed << std::endl;

    return failed > 0 ? 1 : 0;
}

