#ifndef RPG_TEST_FRAMEWORK_H
#define RPG_TEST_FRAMEWORK_H

// Минимальный учебный test-runner. Не замена GoogleTest, но:
// — собирается без зависимостей,
// — макрос CHECK/CHECK_EQ ловит и считает фейлы,
// — в конце print_summary() и возврат кода.
//
// Пример:
//   #include "tests/test_framework.h"
//   int main() {
//       test::Stats s;
//       CHECK(s, 1 + 1 == 2);
//       CHECK_EQ(s, std::string("hi"), "hi");
//       return s.report("simple");
//   }

#include <iostream>
#include <sstream>
#include <string>

namespace test {

struct Stats {
    int total = 0;
    int passed = 0;

    int report(const std::string& suite) const {
        std::cout << "[" << suite << "] " << passed << "/" << total
                  << " прошло\n";
        return (passed == total) ? 0 : 1;
    }
};

}  // namespace test

#define CHECK(stats, cond)                                                  \
    do {                                                                    \
        (stats).total++;                                                    \
        if (cond) {                                                         \
            (stats).passed++;                                               \
        } else {                                                            \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__              \
                      << " — " #cond << "\n";                                \
        }                                                                   \
    } while (0)

#define CHECK_EQ(stats, a, b)                                               \
    do {                                                                    \
        (stats).total++;                                                    \
        auto&& _a = (a);                                                    \
        auto&& _b = (b);                                                    \
        if (_a == _b) {                                                     \
            (stats).passed++;                                               \
        } else {                                                            \
            std::ostringstream _ss;                                         \
            _ss << "FAIL " << __FILE__ << ":" << __LINE__                    \
                << " — " #a " == " #b " (got " << _a << " vs " << _b << ")"; \
            std::cerr << _ss.str() << "\n";                                  \
        }                                                                   \
    } while (0)

#endif
