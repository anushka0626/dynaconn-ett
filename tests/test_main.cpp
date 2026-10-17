#include "TestFramework.hpp"
#include <chrono>
#include <cstring>

int main(int argc, char** argv) {
    auto& cases = dctest::registry();
    if (argc > 1 && std::strcmp(argv[1], "--list") == 0) {
        for (const auto& c : cases) std::printf("%s\n", c.name.c_str());
        return 0;
    }

    int ran = 0, failed = 0;
    for (const auto& c : cases) {
        if (argc > 1 && c.name != argv[1]) continue;
        ++ran;
        auto start = std::chrono::steady_clock::now();
        try {
            c.fn();
            double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::printf("[PASS] %s (%.2fs)\n", c.name.c_str(), s);
        } catch (const std::exception& e) {
            ++failed;
            std::printf("[FAIL] %s\n       %s\n", c.name.c_str(), e.what());
        }
    }

    if (ran == 0) {
        std::printf("no test named '%s'\n", argc > 1 ? argv[1] : "");
        return 2;
    }
    return failed == 0 ? 0 : 1;
}
