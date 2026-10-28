#pragma once

// A tiny test harness: no dependency to fetch, nothing to configure.
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace test {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& cases() {
    static std::vector<Case> c;
    return c;
}
inline int& failures() {
    static int f = 0;
    return f;
}

struct Register {
    Register(const char* name, std::function<void()> fn) { cases().push_back({name, std::move(fn)}); }
};

}  // namespace test

#define TEST(name)                                                   \
    static void name();                                              \
    static test::Register name##_reg(#name, name);                   \
    static void name()

#define CHECK(cond)                                                                         \
    do {                                                                                    \
        if (!(cond)) {                                                                      \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
            ++test::failures();                                                             \
        }                                                                                   \
    } while (0)
