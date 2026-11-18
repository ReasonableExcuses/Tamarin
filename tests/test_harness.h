// ============================================================================
// test_harness.h - Lightweight test registration framework
// ============================================================================
#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

struct TestCase {
    std::string name;
    std::function<bool()> fn;
};

inline std::vector<TestCase>& getRegisteredTests() {
    static std::vector<TestCase> tests;
    return tests;
}

struct TestRegistrar {
    TestRegistrar(const std::string& name, std::function<bool()> fn) {
        getRegisteredTests().push_back({name, std::move(fn)});
    }
};

#define TAMARIN_TEST(suite, name) \
    static bool test_##suite##_##name(); \
    static ::TestRegistrar reg_##suite##_##name(#suite "." #name, test_##suite##_##name); \
    static bool test_##suite##_##name()

#define EXPECT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "  FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            return false; \
        } \
    } while (0)

#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

#define EXPECT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "  FAILED: " #a " == " #b " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            return false; \
        } \
    } while (0)

#define EXPECT_NE(a, b) EXPECT_TRUE((a) != (b))
#define EXPECT_GE(a, b) EXPECT_TRUE((a) >= (b))
#define EXPECT_LE(a, b) EXPECT_TRUE((a) <= (b))
#define EXPECT_GT(a, b) EXPECT_TRUE((a) > (b))
#define EXPECT_LT(a, b) EXPECT_TRUE((a) < (b))

