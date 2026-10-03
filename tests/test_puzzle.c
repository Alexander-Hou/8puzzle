/*
 * puzzle 模块（棋盘状态与启发函数）单元测试。
 *
 * 直接链接 src/puzzle.c，覆盖状态编码、合法性、逆序数可解性、h1/h2/h0
 * 以及"一致性"这一文档中声明依赖的性质。期望值均由独立脚本核对过。
 *
 * 运行：make test-puzzle
 */

#include "puzzle.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ 样例 */

static const int GOAL[3][3] = {
    {1, 2, 3},
    {4, 5, 6},
    {7, 8, 0}
};
static const int S1[3][3] = { /* 可解，最优解较短 */
    {1, 2, 3},
    {4, 0, 6},
    {7, 5, 8}
};
static const int S2[3][3] = { /* 经典不可解样例（逆序数 11） */
    {2, 8, 3},
    {1, 6, 4},
    {7, 0, 5}
};
static const int S3[3][3] = { /* 难例（逆序数 24） */
    {8, 6, 7},
    {2, 5, 4},
    {3, 0, 1}
};
static const int S4[3][3] = { /* 空格在左上 */
    {0, 1, 2},
    {3, 4, 5},
    {6, 7, 8}
};
static const int S5[3][3] = { /* 仅 8 与空格反位 */
    {1, 2, 3},
    {4, 5, 6},
    {7, 0, 8}
};
static const int REVERSE[3][3] = { /* 逆序排列，逆序数 28 */
    {8, 7, 6},
    {5, 4, 3},
    {2, 1, 0}
};

/* ------------------------------------------------------------------ 框架 */

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

/* ------------------------------------------------------------ 辅助函数 */

/* 生成空格上下左右移动得到的合法后继，返回个数 */
static int make_neighbors(const int state[3][3], int out[4][3][3])
{
    static const int DR[4] = {-1, 1, 0, 0};
    static const int DC[4] = {0, 0, -1, 1};
    int blank_row = -1;
    int blank_col = -1;
    int count = 0;
    int i;
    int j;
    int k;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            if (state[i][j] == 0) {
                blank_row = i;
                blank_col = j;
            }
        }
    }
    if (blank_row < 0) {
        return 0;
    }
    for (k = 0; k < 4; k++) {
        int row = blank_row + DR[k];
        int col = blank_col + DC[k];

        if (row < 0 || row > 2 || col < 0 || col > 2) {
            continue;
        }
        memcpy(out[count], state, sizeof(int) * 9);
        out[count][blank_row][blank_col] = out[count][row][col];
        out[count][row][col] = 0;
        count++;
    }
    return count;
}

/* ------------------------------------------------------------------ 用例 */

static void test_encode(void)
{
    begin_case("state_encode 九进制编码");
    CHECK(state_encode(GOAL) == 54480996u);
    CHECK(state_encode(S1) == 54448172u);
    CHECK(state_encode(S2) == 126053420u);
    CHECK(state_encode(S3) == 376945732u);
    CHECK(state_encode(S4) == 6053444u);
    CHECK(state_encode(S5) == 54480932u);
    CHECK(state_encode(REVERSE) == 381367044u);

    /* 编码上界 9^9 - 1 */
    CHECK(state_encode(REVERSE) < 387420489u);

    /* 不同状态编码不同 */
    CHECK(state_encode(GOAL) != state_encode(S1));
    CHECK(state_encode(S4) != state_encode(S5));
    CHECK(state_encode(S2) != state_encode(S3));
    end_case();
}

static void test_valid_state(void)
{
    static const int duplicate[3][3] = {
        {1, 2, 3},
        {4, 5, 6},
        {7, 7, 0}
    };
    static const int out_of_range[3][3] = {
        {1, 2, 3},
        {4, 5, 6},
        {7, 8, 9}
    };
    static const int negative[3][3] = {
        {-1, 2, 3},
        {4, 5, 6},
        {7, 8, 0}
    };

    begin_case("is_valid_state 合法性");
    CHECK(is_valid_state(GOAL));
    CHECK(is_valid_state(S1));
    CHECK(is_valid_state(S2));
    CHECK(is_valid_state(S3));
    CHECK(is_valid_state(S4));
    CHECK(is_valid_state(S5));
    CHECK(is_valid_state(REVERSE));

    CHECK(!is_valid_state(duplicate));
    CHECK(!is_valid_state(out_of_range));
    CHECK(!is_valid_state(negative));
    CHECK(!is_valid_state(NULL));
    end_case();
}

static void test_inversion_count(void)
{
    begin_case("inversion_count 逆序数");
    CHECK(inversion_count(GOAL) == 0);
    CHECK(inversion_count(S1) == 2);
    CHECK(inversion_count(S2) == 11);
    CHECK(inversion_count(S3) == 24);
    CHECK(inversion_count(S4) == 0);
    CHECK(inversion_count(S5) == 0);
    CHECK(inversion_count(REVERSE) == 28);
    CHECK(inversion_count(NULL) == -1);
    end_case();
}

static void test_solvable(void)
{
    static const int duplicate[3][3] = {
        {1, 2, 3},
        {4, 5, 6},
        {7, 7, 0}
    };

    begin_case("is_solvable 可解性");
    CHECK(is_solvable(GOAL, GOAL));
    CHECK(is_solvable(S1, GOAL));
    CHECK(is_solvable(S3, GOAL));
    CHECK(is_solvable(S4, GOAL));
    CHECK(is_solvable(S5, GOAL));
    CHECK(!is_solvable(S2, GOAL));      /* 逆序数奇偶性不同 */
    CHECK(!is_solvable(S2, REVERSE));
    CHECK(is_solvable(S1, REVERSE));    /* 2 与 28 同为偶数 */
    CHECK(is_solvable(S3, S1));

    CHECK(!is_solvable(duplicate, GOAL)); /* 非法棋盘一律判为不可解 */
    CHECK(!is_solvable(NULL, GOAL));
    CHECK(!is_solvable(GOAL, NULL));

    /* 与逆序数奇偶性等价 */
    CHECK(is_solvable(S1, GOAL) == ((inversion_count(S1) & 1) == (inversion_count(GOAL) & 1)));
    CHECK(is_solvable(S2, GOAL) == ((inversion_count(S2) & 1) == (inversion_count(GOAL) & 1)));
    end_case();
}

static void test_misplaced(void)
{
    static const struct {
        const int (*state)[3];
        int expected;
    } cases[] = {
        {GOAL, 0},
        {S1, 2},
        {S2, 6},
        {S3, 7},
        {S4, 8},
        {S5, 1},
        {REVERSE, 8}
    };
    size_t i;

    begin_case("h1 放错数码数");
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CHECK(h_misplaced_tiles(cases[i].state, GOAL) == cases[i].expected);
        CHECK(h_misplaced_tiles(GOAL, cases[i].state) == cases[i].expected); /* 对称 */
    }
    CHECK(h_misplaced_tiles(S4, S4) == 0); /* 任意目标：与自身比较为 0 */
    CHECK(h_misplaced_tiles(S5, S3) == 7); /* 非标准目标，必须按 target 映射计算 */
    end_case();
}

static void test_manhattan(void)
{
    static const struct {
        const int (*state)[3];
        int expected;
    } cases[] = {
        {GOAL, 0},
        {S1, 2},
        {S2, 9},
        {S3, 21},
        {S4, 12},
        {S5, 1},
        {REVERSE, 16}
    };
    size_t i;

    begin_case("h2 曼哈顿距离");
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CHECK(h_manhattan_distance(cases[i].state, GOAL) == cases[i].expected);
        CHECK(h_manhattan_distance(GOAL, cases[i].state) == cases[i].expected); /* 对称 */
    }
    CHECK(h_manhattan_distance(S4, S4) == 0);
    CHECK(h_manhattan_distance(S5, S3) == 22); /* 非标准目标 */
    CHECK(h_manhattan_distance(GOAL, S3) == 21);
    CHECK(h_manhattan_distance(S4, S1) == 12);
    end_case();
}

static void test_zero_and_invariants(void)
{
    static const int (*samples[])[3] = {GOAL, S1, S2, S3, S4, S5, REVERSE};
    size_t i;

    begin_case("h0 与不变量");
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        int misplaced = h_misplaced_tiles(samples[i], GOAL);
        int manhattan = h_manhattan_distance(samples[i], GOAL);

        CHECK(h_zero(samples[i], GOAL) == 0);
        CHECK(misplaced <= manhattan); /* h1 <= h2 恒成立 */
    }
    /* 目标自身三种启发值都为 0 */
    CHECK(h_misplaced_tiles(GOAL, GOAL) == 0);
    CHECK(h_manhattan_distance(GOAL, GOAL) == 0);
    CHECK(h_zero(GOAL, GOAL) == 0);
    end_case();
}

static void test_heuristic_func_pointer(void)
{
    HeuristicFunc funcs[3];
    size_t i;

    funcs[0] = h_misplaced_tiles;
    funcs[1] = h_manhattan_distance;
    funcs[2] = h_zero;

    begin_case("函数指针调用");
    CHECK(funcs[0](S3, GOAL) == h_misplaced_tiles(S3, GOAL));
    CHECK(funcs[1](S3, GOAL) == h_manhattan_distance(S3, GOAL));
    CHECK(funcs[2](S3, GOAL) == 0);
    for (i = 0; i < 3; i++) {
        CHECK(funcs[i] != NULL);
    }
    end_case();
}

static void test_consistency(void)
{
    static const int (*samples[])[3] = {GOAL, S1, S2, S3, S4, S5, REVERSE};
    size_t i;

    begin_case("启发式一致性 |h(n)-h(n')| <= 1");
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        int neighbors[4][3][3];
        int count = make_neighbors(samples[i], neighbors);
        int k;

        CHECK(count > 0);
        for (k = 0; k < count; k++) {
            /* 显式加 const 以满足 C23 之前对多维数组限定符转换的约束（-Wpedantic 下会告警） */
            const int(*next)[3] = (const int(*)[3])neighbors[k];
            int d1 = h_misplaced_tiles(next, GOAL) - h_misplaced_tiles(samples[i], GOAL);
            int d2 = h_manhattan_distance(next, GOAL) - h_manhattan_distance(samples[i], GOAL);

            if (d1 < 0) {
                d1 = -d1;
            }
            if (d2 < 0) {
                d2 = -d2;
            }
            CHECK(d1 <= 1);
            CHECK(d2 <= 1);
        }
    }
    end_case();
}

int main(void)
{
    test_encode();
    test_valid_state();
    test_inversion_count();
    test_solvable();
    test_misplaced();
    test_manhattan();
    test_zero_and_invariants();
    test_heuristic_func_pointer();
    test_consistency();

    printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
