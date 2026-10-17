#pragma once
#include <cstdio>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Minimal dependency-free test registry. Each TEST_CASE is run by name from ctest:
//   dc_tests <name>     run one case
//   dc_tests --list     list cases
//   dc_tests            run all

namespace dctest {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] inline void fail(const char* file, int line, const std::string& what) {
    std::ostringstream os;
    os << file << ":" << line << ": " << what;
    throw Failure(os.str());
}

}  // namespace dctest

#define DC_CONCAT_INNER(a, b) a##b
#define DC_CONCAT(a, b) DC_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                                      \
    static void DC_CONCAT(test_fn_, __LINE__)();                                             \
    static ::dctest::Registrar DC_CONCAT(test_reg_, __LINE__)(name, DC_CONCAT(test_fn_, __LINE__)); \
    static void DC_CONCAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) ::dctest::fail(__FILE__, __LINE__, "CHECK(" #cond ") failed"); \
    } while (0)

#define CHECK_MSG(cond, msg)                                                       \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::ostringstream dc_os_;                                             \
            dc_os_ << "CHECK(" #cond ") failed: " << msg;                          \
            ::dctest::fail(__FILE__, __LINE__, dc_os_.str());                      \
        }                                                                          \
    } while (0)
