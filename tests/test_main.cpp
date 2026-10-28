#include "check.hpp"

int main() {
    for (auto& c : test::cases()) {
        const int before = test::failures();
        c.fn();
        std::printf("%s %s\n", test::failures() == before ? "ok  " : "FAIL", c.name.c_str());
    }
    std::printf("%zu tests, %d failures\n", test::cases().size(), test::failures());
    return test::failures() == 0 ? 0 : 1;
}
