#ifndef TEST_UTIL_H
#define TEST_UTIL_H

/* 各测试文件共用的极简断言框架（每个测试文件单独编译，静态符号互不干扰） */

#include <stdio.h>

static int g_checks = 0;
static int g_failures = 0;
static int g_case_failures = 0;
static const char* g_case = "";

static void check_impl(int cond, const char* expr, int line)
{
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("  [%s] FAIL line %d: %s\n", g_case, line, expr);
    }
}

#define CHECK(cond) check_impl((cond) ? 1 : 0, #cond, __LINE__)

static void begin_case(const char* name)
{
    g_case = name;
    g_case_failures = g_failures;
}

static void end_case(void)
{
    printf("%s  %s\n", (g_failures == g_case_failures) ? "PASS" : "FAIL", g_case);
}

static int test_summary(void)
{
    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}

#endif /* TEST_UTIL_H */
