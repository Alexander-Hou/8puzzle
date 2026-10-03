/*
 * cli 模块单元测试。
 *
 * 直接链接 src/cli.c 调用 parse_cli_args / parse_state_string，
 * 断言返回值与 Config 字段，不依赖主程序或整程序构建。
 *
 * 运行：make test-cli
 */
#define _POSIX_C_SOURCE 200809L

#include "cli.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_ARGS 32

#define SOLVABLE "1 2 3 4 0 6 7 5 8"

static const int STD_TARGET[3][3] = {
    {1, 2, 3},
    {4, 5, 6},
    {7, 8, 0}
};

static int g_checks = 0;
static int g_failures = 0;
static int g_case_failures = 0;
static const char* g_case = "";

static void check(int cond, const char* expr, int line)
{
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("  [%s] FAIL line %d: %s\n", g_case, line, expr);
    }
}

#define CHECK(cond) check((cond) ? 1 : 0, #cond, __LINE__)

static void begin_case(const char* name)
{
    g_case = name;
    g_case_failures = g_failures;
}

static void end_case(void)
{
    printf("%s  %s\n", (g_failures == g_case_failures) ? "PASS" : "FAIL", g_case);
}

/* ---------------------------------------------------------------- 辅助工具 */

/* 被测模块对非法输入会向指定 fd 输出诊断，测试期间临时屏蔽以免淹没结果 */
static int g_saved_stderr = -1;

static void mute_fd(int fd, int* saved)
{
    int devnull;

    fflush(NULL);
    *saved = dup(fd);
    devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
        dup2(devnull, fd);
        close(devnull);
    }
}

static void unmute_fd(int fd, int* saved)
{
    if (*saved >= 0) {
        fflush(NULL);
        dup2(*saved, fd);
        close(*saved);
        *saved = -1;
    }
}

typedef struct {
    CliStatus rc;
    Config cfg;
} Result;

/* 以 "puzzle <args...>" 的形式调用被测函数；args 以 NULL 结尾 */
static Result run_list(const char* const* args)
{
    char* argv[MAX_ARGS + 2];
    Result r;
    int argc = 1;
    int i;

    argv[0] = (char*)"puzzle";
    for (i = 0; args[i] != NULL; i++) {
        if (argc > MAX_ARGS) {
            fprintf(stderr, "test_cli: 参数过多\n");
            exit(2);
        }
        argv[argc++] = (char*)args[i];
    }
    argv[argc] = NULL;

    memset(&r.cfg, 0xA5, sizeof(r.cfg)); /* 脏值填充，便于发现未被写入的字段 */
    mute_fd(STDERR_FILENO, &g_saved_stderr);
    r.rc = parse_cli_args(argc, argv, &r.cfg);
    unmute_fd(STDERR_FILENO, &g_saved_stderr);
    return r;
}

/* RUN(...) 以 NULL 作为哨兵，避免手工维护参数个数 */
#define RUN(...) run_list((const char*[]){__VA_ARGS__, NULL})

/* 参数用 const void* 接收，避免 C23 之前 int(*)[3] 与 const int(*)[3] 的限定符转换告警 */
static int state_equals(const void* lhs, const void* rhs)
{
    const int* a = (const int*)lhs;
    const int* b = (const int*)rhs;
    int i;

    for (i = 0; i < 9; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/* -------------------------------------------------------------------- 用例 */

static void test_defaults(void)
{
    static const int expected_start[3][3] = {
        {1, 2, 3},
        {4, 0, 6},
        {7, 5, 8}
    };
    Result r = RUN("-s", SOLVABLE);

    begin_case("默认值");
    CHECK(r.rc == CLI_RC_OK);
    CHECK(r.cfg.has_start == 1);
    CHECK(r.cfg.method == METHOD_H2);
    CHECK(r.cfg.repeat == 1);
    CHECK(r.cfg.benchmark == 0);
    CHECK(r.cfg.seed == CLI_DEFAULT_SEED);
    CHECK(r.cfg.csv_path == NULL);
    CHECK(r.cfg.print_path == 0);
    CHECK(r.cfg.selftest == 0);
    CHECK(state_equals(r.cfg.start, expected_start));
    CHECK(state_equals(r.cfg.target, STD_TARGET));
    end_case();
}

static void test_long_options(void)
{
    static const int expected_target[3][3] = {
        {8, 7, 6},
        {5, 4, 3},
        {2, 1, 0}
    };
    Result r = RUN("--start", SOLVABLE,
                   "--target", "8 7 6 5 4 3 2 1 0",
                   "--method", "bfs",
                   "--print-path",
                   "--repeat", "3",
                   "--benchmark", "4",
                   "--seed", "7",
                   "--csv", "out.csv");

    begin_case("长选项全字段");
    CHECK(r.rc == CLI_RC_OK);
    CHECK(state_equals(r.cfg.target, expected_target));
    CHECK(r.cfg.method == METHOD_BFS);
    CHECK(r.cfg.print_path == 1);
    CHECK(r.cfg.repeat == 3);
    CHECK(r.cfg.benchmark == 4);
    CHECK(r.cfg.seed == 7u);
    CHECK(r.cfg.csv_path != NULL && strcmp(r.cfg.csv_path, "out.csv") == 0);
    end_case();
}

static void test_method(void)
{
    begin_case("策略映射");
    CHECK(RUN("-s", SOLVABLE, "-m", "h1").cfg.method == METHOD_H1);
    CHECK(RUN("-s", SOLVABLE, "-m", "h2").cfg.method == METHOD_H2);
    CHECK(RUN("-s", SOLVABLE, "-m", "bfs").cfg.method == METHOD_BFS);
    CHECK(RUN("-s", SOLVABLE, "-m", "all").cfg.method == METHOD_ALL);
    CHECK(RUN("-s", SOLVABLE, "--method", "all").cfg.method == METHOD_ALL);
    end_case();

    begin_case("非法策略");
    CHECK(RUN("-s", SOLVABLE, "-m", "h3").rc == CLI_RC_ERROR);
    CHECK(RUN("-s", SOLVABLE, "-m", "H1").rc == CLI_RC_ERROR);
    CHECK(RUN("-s", SOLVABLE, "-m", "").rc == CLI_RC_ERROR);
    end_case();
}

static void test_flags(void)
{
    begin_case("打印路径标志");
    CHECK(RUN("-s", SOLVABLE, "-p").cfg.print_path == 1);
    CHECK(RUN("-s", SOLVABLE, "--print-path").cfg.print_path == 1);
    CHECK(RUN("-s", SOLVABLE).cfg.print_path == 0);
    end_case();
}

static void test_repeat(void)
{
    begin_case("-r 重复次数");
    CHECK(RUN("-s", SOLVABLE, "-r", "5").cfg.repeat == 5);
    CHECK(RUN("-s", SOLVABLE, "-r", "1").cfg.repeat == 1);
    CHECK(RUN("-s", SOLVABLE, "-r", "2147483647").cfg.repeat == 2147483647);
    CHECK(RUN("-s", SOLVABLE, "-r", "0").rc == CLI_RC_ERROR);
    CHECK(RUN("-s", SOLVABLE, "-r", "-1").rc == CLI_RC_ERROR);
    CHECK(RUN("-s", SOLVABLE, "-r", "abc").rc == CLI_RC_ERROR);
    CHECK(RUN("-s", SOLVABLE, "-r", "2x").rc == CLI_RC_ERROR);
    CHECK(RUN("-s", SOLVABLE, "-r", "2147483648").rc == CLI_RC_ERROR);
    end_case();
}

static void test_benchmark(void)
{
    begin_case("-b 批量规模");
    CHECK(RUN("-s", SOLVABLE, "-b", "5").cfg.benchmark == 5);
    CHECK(RUN("-s", SOLVABLE, "--benchmark", "10").cfg.benchmark == 10);
    CHECK(RUN("-s", SOLVABLE, "-b", "0").rc == CLI_RC_ERROR);
    CHECK(RUN("-s", SOLVABLE, "-b", "abc").rc == CLI_RC_ERROR);
    end_case();
}

static void test_seed(void)
{
    begin_case("--seed 边界");
    CHECK(RUN("-s", SOLVABLE, "--seed", "0").cfg.seed == 0u);
    CHECK(RUN("-s", SOLVABLE, "--seed", "4294967295").cfg.seed == 4294967295u);
    CHECK(RUN("-s", SOLVABLE, "--seed", "4294967296").rc == CLI_RC_ERROR);
    CHECK(RUN("-s", SOLVABLE, "--seed", "abc").rc == CLI_RC_ERROR);
    CHECK(RUN("-s", SOLVABLE, "--seed", "-1").rc == CLI_RC_ERROR);
    end_case();
}

static void test_selftest(void)
{
    begin_case("--selftest 可省略 -s");
    {
        Result r = RUN("--selftest");
        CHECK(r.rc == CLI_RC_OK);
        CHECK(r.cfg.selftest == 1);
        CHECK(r.cfg.has_start == 0);
        CHECK(state_equals(r.cfg.target, STD_TARGET));
    }
    {
        Result r = RUN("--selftest", "-s", SOLVABLE);
        CHECK(r.rc == CLI_RC_OK);
        CHECK(r.cfg.selftest == 1 && r.cfg.has_start == 1);
    }
    end_case();
}

static void test_missing_start(void)
{
    begin_case("缺少 -s");
    CHECK(RUN(NULL).rc == CLI_RC_ERROR);
    CHECK(RUN("-t", "1 2 3 4 5 6 7 8 0").rc == CLI_RC_ERROR);
    CHECK(RUN("-p").rc == CLI_RC_ERROR);
    CHECK(RUN("-m", "all").rc == CLI_RC_ERROR);
    /* 批量基准与自检模式不需要 -s */
    CHECK(RUN("-b", "5").rc == CLI_RC_OK);
    CHECK(RUN("--benchmark", "5").cfg.benchmark == 5);
    end_case();
}

static void test_help(void)
{
    begin_case("-h / --help");
    CHECK(RUN("-h").rc == CLI_RC_HELP);
    CHECK(RUN("--help").rc == CLI_RC_HELP);
    CHECK(RUN("-h").cfg.method == METHOD_H2); /* 帮助返回时已写入默认值 */
    end_case();
}

static void test_usage_errors(void)
{
    begin_case("用法类错误");
    CHECK(RUN("-z").rc == CLI_RC_ERROR);
    CHECK(RUN("-s", SOLVABLE, "--nope").rc == CLI_RC_ERROR);
    CHECK(RUN("-s").rc == CLI_RC_ERROR);       /* 缺少选项参数 */
    CHECK(RUN("--start").rc == CLI_RC_ERROR);  /* 长选项缺少参数 */
    CHECK(RUN("-s", SOLVABLE, "extra").rc == CLI_RC_ERROR);
    end_case();
}

static void test_invalid_target(void)
{
    begin_case("非法目标状态");
    CHECK(RUN("-s", SOLVABLE, "-t", "1 2 3").rc == CLI_RC_ERROR);
    CHECK(RUN("-s", SOLVABLE, "-t", "1 2 3 4 5 6 7 8 8").rc == CLI_RC_ERROR);
    CHECK(RUN("-s", SOLVABLE, "-t", "1 2 3 4 5 6 7 8 0").rc == CLI_RC_OK);
    end_case();
}

static void test_state_string(void)
{
    static const struct {
        const char* text;
        StateParseStatus want;
    } cases[] = {
        {"1 2 3 4 5 6 7 8 0",        STATE_OK},
        {"  1  2\t3\n4 5 6 7 8 0  ", STATE_OK},
        {"1 2 3",                    STATE_ERR_COUNT},
        {"",                         STATE_ERR_COUNT},
        {"1 2 3 4 5 6 7 8",          STATE_ERR_COUNT},
        {"1 2 3 4 5 6 7 8 0 0",      STATE_ERR_COUNT},
        {"1 2 3 4 5 6 7 8 x",        STATE_ERR_NOT_NUMBER},
        {"1 2 3 4 5 6 7 8 9",        STATE_ERR_RANGE},
        {"-1 2 3 4 5 6 7 8 0",       STATE_ERR_RANGE},
        {"1 2 3 4 5 6 7 7 0",        STATE_ERR_DUPLICATE}
    };
    static const int expected[3][3] = {
        {1, 2, 3},
        {4, 5, 6},
        {7, 8, 0}
    };
    size_t i;

    begin_case("parse_state_string 判定");
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int state[3][3];
        StateParseStatus got = parse_state_string(cases[i].text, state);

        CHECK(got == cases[i].want);
        if (cases[i].want == STATE_OK) {
            CHECK(state_equals(state, expected));
        }
    }
    end_case();
}

static void test_reuse(void)
{
    Result a;
    Result b;

    begin_case("重复调用（optind 复位）");
    a = RUN("-s", SOLVABLE);
    b = RUN("-s", SOLVABLE, "-p");
    CHECK(a.rc == CLI_RC_OK && a.cfg.print_path == 0);
    CHECK(b.rc == CLI_RC_OK && b.cfg.print_path == 1);
    a = RUN("-s", SOLVABLE, "-m", "all");
    b = RUN("-s", SOLVABLE);
    CHECK(a.cfg.method == METHOD_ALL);
    CHECK(b.cfg.method == METHOD_H2); /* 上一次的 -m 不残留 */
    end_case();
}

static void test_print_usage(void)
{
    int saved = -1;

    begin_case("print_usage 不崩溃");
    mute_fd(STDOUT_FILENO, &saved);
    print_usage("/usr/bin/puzzle");
    print_usage(NULL);
    unmute_fd(STDOUT_FILENO, &saved);
    CHECK(1);
    end_case();
}

int main(void)
{
    test_defaults();
    test_long_options();
    test_method();
    test_flags();
    test_repeat();
    test_benchmark();
    test_seed();
    test_selftest();
    test_missing_start();
    test_help();
    test_usage_errors();
    test_invalid_target();
    test_state_string();
    test_reuse();
    test_print_usage();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
