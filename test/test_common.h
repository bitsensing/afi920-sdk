/**
 * @file test_common.h
 * @brief Shared minimal test harness (framework-free, CTest exit code)
 */
#pragma once

#include <cstdio>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct _reg_##name { _reg_##name() { test_##name(); } } _inst_##name; \
    static void test_##name()

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            printf("  FAIL: %s (line %d)\n", #expr, __LINE__); \
            tests_failed++; \
            return; \
        } \
    } while(0)

#define PASS(name) \
    do { printf("  PASS: %s\n", name); tests_passed++; } while(0)

#define TEST_MAIN(suite_name) \
    int main() { \
        printf("=== %s ===\n", suite_name); \
        printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed); \
        return tests_failed > 0 ? 1 : 0; \
    }
