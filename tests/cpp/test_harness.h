//
// Lightweight test harness for Angara compiler unit tests.
// No external dependencies — just include and use.
//
// Usage:
//   #include "test_harness.h"
//   TEST("description") { ... assert stuff ... }
//   MAIN()
//

#ifndef ANGARA_TEST_HARNESS_H
#define ANGARA_TEST_HARNESS_H

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <cmath>
#include <cstring>

namespace angara::test {

struct Test {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Test>& tests() {
    static std::vector<Test> t;
    return t;
}

inline int& fail_count() {
    static int c = 0;
    return c;
}

inline int& pass_count() {
    static int c = 0;
    return c;
}

inline bool& current_failed() {
    static bool f = false;
    return f;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        tests().push_back({name, std::move(fn)});
    }
};

#define TEST(name) \
    static void test_##name(); \
    static angara::test::Registrar reg_##name(#name, test_##name); \
    static void test_##name()

#define MAIN() \
    int main() { \
        const auto& t = angara::test::tests(); \
        int run = 0; \
        for (const auto& test : t) { \
            angara::test::current_failed() = false; \
            run++; (void)run; \
            try { test.fn(); } \
            catch (const std::exception& e) { \
                std::cout << "  \033[1;31mTHREW\033[0m " << test.name << ": " << e.what() << "\n"; \
                angara::test::fail_count()++; \
                continue; \
            } catch (...) { \
                std::cout << "  \033[1;31mTHREW\033[0m " << test.name << ": unknown exception\n"; \
                angara::test::fail_count()++; \
                continue; \
            } \
            if (angara::test::current_failed()) { \
                angara::test::fail_count()++; \
            } else { \
                std::cout << "  \033[1;32mPASS\033[0m " << test.name << "\n"; \
                angara::test::pass_count()++; \
            } \
        } \
        int total = angara::test::pass_count() + angara::test::fail_count(); \
        std::cout << "\n  " << total << " tests: " \
                  << "\033[1;32m" << angara::test::pass_count() << " passed\033[0m"; \
        if (angara::test::fail_count() > 0) \
            std::cout << ", \033[1;31m" << angara::test::fail_count() << " failed\033[0m"; \
        std::cout << "\n"; \
        return angara::test::fail_count(); \
    }

// ── Assertions ──

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) { \
        std::cout << "  \033[1;31mFAIL\033[0m " << __func__ << " at " << __FILE__ << ":" << __LINE__ \
                  << ": " << #expr << " was false\n"; \
        angara::test::current_failed() = true; return; \
    }} while(0)

#define ASSERT_FALSE(expr) \
    do { if ((expr)) { \
        std::cout << "  \033[1;31mFAIL\033[0m " << __func__ << " at " << __FILE__ << ":" << __LINE__ \
                  << ": " << #expr << " was true\n"; \
        angara::test::current_failed() = true; return; \
    }} while(0)

#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) { \
        std::cout << "  \033[1;31mFAIL\033[0m " << __func__ << " at " << __FILE__ << ":" << __LINE__ \
                  << ": " << #a << " != " << #b << "\n"; \
        angara::test::current_failed() = true; return; \
    }} while(0)

#define ASSERT_NEQ(a, b) \
    do { if ((a) == (b)) { \
        std::cout << "  \033[1;31mFAIL\033[0m " << __func__ << " at " << __FILE__ << ":" << __LINE__ \
                  << ": " << #a << " == " << #b << " (expected unequal)\n"; \
        angara::test::current_failed() = true; return; \
    }} while(0)

#define ASSERT_THROWS(expr) \
    do { bool threw = false; try { expr; } catch (...) { threw = true; } \
    if (!threw) { \
        std::cout << "  \033[1;31mFAIL\033[0m " << __func__ << " at " << __FILE__ << ":" << __LINE__ \
                  << ": " << #expr << " did not throw\n"; \
        angara::test::current_failed() = true; return; \
    }} while(0)

} // namespace angara::test

#endif // ANGARA_TEST_HARNESS_H
