/*
 * 程序入口：串联 CLI 解析 -> 可解性判定 -> A* 搜索 -> 结果输出。
 *
 * 退出码（与 docs/01-requirements.md 2.6 一致）：
 *   0 求解成功；1 参数或输入非法；2 无解；3 内存不足/超出节点上限；4 自检失败
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "pool.h"
#include "puzzle.h"
#include "reporter.h"

enum {
    EXIT_RC_OK = 0,
    EXIT_RC_USAGE = 1,
    EXIT_RC_UNSOLVABLE = 2,
    EXIT_RC_RESOURCE = 3,
    EXIT_RC_SELFTEST = 4
};

typedef struct {
    Method method;
    const char* name;
    HeuristicFunc func;
} MethodSpec;

/* 输出顺序固定为 h1、h2、bfs，对比表与 CSV 均依赖该顺序（bfs 为剪枝率基准） */
static const MethodSpec METHODS[3] = {
    {METHOD_H1, "h1", h_misplaced_tiles},
    {METHOD_H2, "h2", h_manhattan_distance},
    {METHOD_BFS, "bfs", h_zero}
};

static const MethodSpec* find_method_spec(Method method)
{
    int i;

    for (i = 0; i < 3; i++) {
        if (METHODS[i].method == method) {
            return &METHODS[i];
        }
    }
    return NULL;
}

static int states_equal(const int a[3][3], const int b[3][3])
{
    int i;
    int j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            if (a[i][j] != b[i][j]) {
                return 0;
            }
        }
    }
    return 1;
}

static void format_state(const int state[3][3], char* buffer, size_t size)
{
    snprintf(buffer, size, "%d %d %d %d %d %d %d %d %d",
             state[0][0], state[0][1], state[0][2],
             state[1][0], state[1][1], state[1][2],
             state[2][0], state[2][1], state[2][2]);
}

/* ------------------------------------------------------- 自检用的路径校验 */

/* from -> to 是否为一次合法的空格移动 */
static int is_legal_move(const int from[3][3], const int to[3][3])
{
    int from_row = -1;
    int from_col = -1;
    int to_row = -1;
    int to_col = -1;
    int diff = 0;
    int drow;
    int dcol;
    int i;
    int j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            if (from[i][j] != to[i][j]) {
                diff++;
                if (from[i][j] == 0) {
                    from_row = i;
                    from_col = j;
                }
                if (to[i][j] == 0) {
                    to_row = i;
                    to_col = j;
                }
            }
        }
    }
    if (diff != 2 || from_row < 0 || to_row < 0) {
        return 0;
    }
    drow = from_row - to_row;
    dcol = from_col - to_col;
    if (drow < 0) {
        drow = -drow;
    }
    if (dcol < 0) {
        dcol = -dcol;
    }
    return (drow + dcol == 1) && from[to_row][to_col] == to[from_row][from_col]
        && from[to_row][to_col] != 0;
}

static int path_is_valid(const Node* goal, const int start[3][3], const int target[3][3])
{
    const Node* chain[64];
    int length = 0;
    const Node* current = goal;
    int i;

    while (current != NULL && length < 64) {
        chain[length++] = current;
        current = current->parent;
    }
    if (length == 0 || current != NULL) {
        return 0;
    }
    if (!states_equal(chain[length - 1]->state, start) || !states_equal(chain[0]->state, target)) {
        return 0;
    }
    for (i = length - 1; i > 0; i--) {
        if (!is_legal_move(chain[i]->state, chain[i - 1]->state)) {
            return 0;
        }
    }
    return 1;
}

/* ----------------------------------------------------------- 重复测量统计 */

static int cmp_long_asc(const void* a, const void* b)
{
    long x = *(const long*)a;
    long y = *(const long*)b;

    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

static int cmp_double_asc(const void* a, const void* b)
{
    double x = *(const double*)a;
    double y = *(const double*)b;

    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

static void stats_long(long* buffer, int count, long* median, long* low, long* high)
{
    qsort(buffer, (size_t)count, sizeof(long), cmp_long_asc);
    *low = buffer[0];
    *high = buffer[count - 1];
    *median = buffer[(count - 1) / 2]; /* 偶数次取中间偏小者 */
}

static void stats_double(double* buffer, int count, double* median, double* low, double* high)
{
    qsort(buffer, (size_t)count, sizeof(double), cmp_double_asc);
    *low = buffer[0];
    *high = buffer[count - 1];
    *median = buffer[(count - 1) / 2];
}

/* 返回 0 成功，-1 表示分配失败 */
static int compute_stats(const SearchMetrics* samples, int count,
                         SearchMetrics* median, SearchMetrics* low, SearchMetrics* high)
{
    long* long_buffer = (long*)malloc(sizeof(long) * (size_t)count);
    double* double_buffer = (double*)malloc(sizeof(double) * (size_t)count);
    int i;

    if (long_buffer == NULL || double_buffer == NULL) {
        free(long_buffer);
        free(double_buffer);
        return -1;
    }

    for (i = 0; i < count; i++) {
        long_buffer[i] = samples[i].generated;
    }
    stats_long(long_buffer, count, &median->generated, &low->generated, &high->generated);

    for (i = 0; i < count; i++) {
        long_buffer[i] = samples[i].expanded;
    }
    stats_long(long_buffer, count, &median->expanded, &low->expanded, &high->expanded);

    for (i = 0; i < count; i++) {
        long_buffer[i] = samples[i].peak_open;
    }
    stats_long(long_buffer, count, &median->peak_open, &low->peak_open, &high->peak_open);

    for (i = 0; i < count; i++) {
        long_buffer[i] = samples[i].peak_closed;
    }
    stats_long(long_buffer, count, &median->peak_closed, &low->peak_closed, &high->peak_closed);

    for (i = 0; i < count; i++) {
        long_buffer[i] = samples[i].total_alloc_nodes;
    }
    stats_long(long_buffer, count, &median->total_alloc_nodes,
               &low->total_alloc_nodes, &high->total_alloc_nodes);

    for (i = 0; i < count; i++) {
        double_buffer[i] = samples[i].time_ms;
    }
    stats_double(double_buffer, count, &median->time_ms, &low->time_ms, &high->time_ms);

    /* 路径长度是确定量：三种策略一致，各次运行也一致 */
    median->path_len = samples[0].path_len;
    low->path_len = samples[0].path_len;
    high->path_len = samples[0].path_len;

    free(long_buffer);
    free(double_buffer);
    return 0;
}

/* ------------------------------------------------------------- 单次/对比模式 */

static int run_single(const Config* config)
{
    const int(*start)[3] = config->start;
    const int(*target)[3] = config->target;
    const MethodSpec* selected[3] = {NULL, NULL, NULL};
    Pool* pools[3] = {NULL, NULL, NULL};
    Node* goals[3] = {NULL, NULL, NULL};
    SearchMetrics median[3];
    SearchMetrics low[3];
    SearchMetrics high[3];
    SearchMetrics* samples = NULL;
    const char* names[3] = {NULL, NULL, NULL};
    int selected_count;
    int bfs_index = -1;
    int path_index = -1;
    int rc = EXIT_RC_OK;
    FILE* csv = NULL;
    int i;
    int r;

    if (!is_solvable(start, target)) {
        printf("无解：初始状态与目标状态的逆序数奇偶性不同。\n");
        return EXIT_RC_UNSOLVABLE;
    }

    if (config->method == METHOD_ALL) {
        selected_count = 3;
        for (i = 0; i < 3; i++) {
            selected[i] = &METHODS[i];
        }
    } else {
        selected_count = 1;
        selected[0] = find_method_spec(config->method);
        if (selected[0] == NULL) {
            return EXIT_RC_USAGE;
        }
    }
    for (i = 0; i < selected_count; i++) {
        names[i] = selected[i]->name;
        if (selected[i]->method == METHOD_BFS) {
            bfs_index = i;
        }
    }
    if (config->print_path) {
        path_index = 0;
        if (config->method == METHOD_ALL) {
            for (i = 0; i < 3; i++) {
                if (METHODS[i].method == METHOD_H2) {
                    path_index = i;
                }
            }
        }
    }

    samples = (SearchMetrics*)malloc(sizeof(SearchMetrics) * (size_t)config->repeat);
    if (samples == NULL) {
        return EXIT_RC_RESOURCE;
    }

    for (i = 0; i < selected_count; i++) {
        for (r = 0; r < config->repeat; r++) {
            Pool* pool = create_pool(0);
            Node* goal;

            if (pool == NULL) {
                rc = EXIT_RC_RESOURCE;
                goto cleanup;
            }
            goal = astar_solve(start, target, selected[i]->func, &samples[r], pool);
            if (goal == NULL) {
                free_pool(pool);
                rc = EXIT_RC_RESOURCE;
                goto cleanup;
            }
            if (r == 0) {
                pools[i] = pool; /* 保留首次运行的池，供路径打印使用 */
                goals[i] = goal;
            } else {
                free_pool(pool);
            }
        }
        if (compute_stats(samples, config->repeat, &median[i], &low[i], &high[i]) != 0) {
            rc = EXIT_RC_RESOURCE;
            goto cleanup;
        }
    }

    if (selected_count == 1) {
        print_metrics_report(&median[0],
                             (config->repeat > 1) ? &low[0] : NULL,
                             (config->repeat > 1) ? &high[0] : NULL,
                             names[0]);
    } else {
        print_comparison_table(median, names, selected_count);
    }

    if (path_index >= 0) {
        printf("\n%s 的完整解路径（共 %d 步）:\n", names[path_index], median[path_index].path_len);
        print_solution_path(goals[path_index]);
    }

    if (config->csv_path != NULL) {
        csv = fopen(config->csv_path, "w");
        if (csv == NULL) {
            fprintf(stderr, "错误：无法写入 CSV 文件 '%s'\n", config->csv_path);
            rc = EXIT_RC_USAGE;
            goto cleanup;
        }
        write_csv_header(csv);
        for (i = 0; i < selected_count; i++) {
            double pruning = NAN; /* 单策略模式无基准，该列留空 */

            if (selected_count == 3) {
                pruning = (1.0 - (double)median[i].generated
                                 / (double)median[bfs_index].generated) * 100.0;
            }
            write_csv_row(csv, start, target, names[i], &median[i], pruning);
        }
        fclose(csv);
        csv = NULL;
    }

cleanup:
    if (csv != NULL) {
        fclose(csv);
    }
    for (i = 0; i < 3; i++) {
        free_pool(pools[i]);
    }
    free(samples);
    return rc;
}

/* --------------------------------------------------------------- 批量基准 */

/* xorshift32：确定性、可复现，状态不能为 0 */
static uint32_t next_random(uint32_t* state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* 生成一个可解且不等于目标的随机初始状态（仅依赖 seed） */
static void generate_solvable_start(uint32_t* rng, int out[3][3], const int target[3][3])
{
    for (;;) {
        int values[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        int i;

        for (i = 8; i > 0; i--) {
            int j = (int)(next_random(rng) % (uint32_t)(i + 1));
            int tmp = values[i];

            values[i] = values[j];
            values[j] = tmp;
        }
        for (i = 0; i < 9; i++) {
            out[i / 3][i % 3] = values[i];
        }

        /* C23 之前 int(*)[3] 不能隐式转为 const int(*)[3]，这里显式加 const */
        if ((inversion_count((const int(*)[3])out) & 1) != (inversion_count(target) & 1)) {
            /* 交换两个非空格数码即可翻转逆序数奇偶性 */
            int first = -1;
            int second = -1;

            for (i = 0; i < 9; i++) {
                if (out[i / 3][i % 3] == 0) {
                    continue;
                }
                if (first < 0) {
                    first = i;
                } else {
                    second = i;
                    break;
                }
            }
            if (second >= 0) {
                int tmp = out[first / 3][first % 3];

                out[first / 3][first % 3] = out[second / 3][second % 3];
                out[second / 3][second % 3] = tmp;
            }
        }

        if (!states_equal((const int(*)[3])out, target)) {
            break; /* 跳过平凡实例 */
        }
    }
}

static int run_benchmark(const Config* config)
{
    const int(*target)[3] = config->target;
    uint32_t rng = (uint32_t)config->seed;
    double sum_length[3] = {0.0, 0.0, 0.0};
    double sum_generated[3] = {0.0, 0.0, 0.0};
    double sum_expanded[3] = {0.0, 0.0, 0.0};
    double sum_peak_open[3] = {0.0, 0.0, 0.0};
    double sum_time[3] = {0.0, 0.0, 0.0};
    double sum_pruning[3] = {0.0, 0.0, 0.0};
    char target_text[32];
    FILE* csv = NULL;
    int rc = EXIT_RC_OK;
    int instance;
    int k;

    if (rng == 0u) {
        rng = CLI_DEFAULT_SEED; /* xorshift 状态不能为 0 */
    }
    format_state(target, target_text, sizeof(target_text));

    if (config->csv_path != NULL) {
        csv = fopen(config->csv_path, "w");
        if (csv == NULL) {
            fprintf(stderr, "错误：无法写入 CSV 文件 '%s'\n", config->csv_path);
            return EXIT_RC_USAGE;
        }
        write_csv_header(csv);
    }

    for (instance = 0; instance < config->benchmark; instance++) {
        int start[3][3];
        SearchMetrics metrics[3];

        generate_solvable_start(&rng, start, target);

        for (k = 0; k < 3; k++) {
            Pool* pool = create_pool(0);
            Node* goal;

            if (pool == NULL) {
                rc = EXIT_RC_RESOURCE;
                goto cleanup;
            }
            goal = astar_solve((const int(*)[3])start, target, METHODS[k].func, &metrics[k], pool);
            free_pool(pool);
            if (goal == NULL) {
                rc = EXIT_RC_RESOURCE;
                goto cleanup;
            }
        }

        for (k = 0; k < 3; k++) {
            double pruning =
                (1.0 - (double)metrics[k].generated / (double)metrics[2].generated) * 100.0;

            sum_length[k] += (double)metrics[k].path_len;
            sum_generated[k] += (double)metrics[k].generated;
            sum_expanded[k] += (double)metrics[k].expanded;
            sum_peak_open[k] += (double)metrics[k].peak_open;
            sum_time[k] += metrics[k].time_ms;
            sum_pruning[k] += pruning;

            if (csv != NULL) {
                write_csv_row(csv, (const int(*)[3])start, target, METHODS[k].name,
                              &metrics[k], pruning);
            }
        }
    }

    printf("批量基准：N=%d，seed=%u，目标=%s\n", config->benchmark, config->seed, target_text);
    printf("%-5s %-13s %-13s %-13s %-13s %-13s %s\n",
           "策略", "平均路径长度", "平均生成节点", "平均扩展节点", "平均OPEN峰值", "平均耗时(ms)",
           "平均剪枝率");
    for (k = 0; k < 3; k++) {
        double n = (double)config->benchmark;

        printf("%-5s %-13.2f %-13.1f %-13.1f %-13.1f %-13.3f %.2f%%\n",
               METHODS[k].name, sum_length[k] / n, sum_generated[k] / n, sum_expanded[k] / n,
               sum_peak_open[k] / n, sum_time[k] / n, sum_pruning[k] / n);
    }

cleanup:
    if (csv != NULL) {
        fclose(csv);
    }
    return rc;
}

/* -------------------------------------------------------------- 内建自检 */

static void report_check(int ok, const char* description, int* failures)
{
    printf("%s  %s\n", ok ? "PASS" : "FAIL", description);
    if (!ok) {
        (*failures)++;
    }
}

static int run_selftest(void)
{
    static const int GOAL[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 0}};
    static const int S1[3][3] = {{1, 2, 3}, {4, 0, 6}, {7, 5, 8}};
    static const int S4[3][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}};
    static const int UNSOLVABLE[3][3] = {{2, 8, 3}, {1, 6, 4}, {7, 0, 5}};
    static const int DUPLICATE[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 7, 0}};
    static const struct {
        const int(*state)[3];
        int expected_length;
        const char* name;
    } cases[] = {
        {S1, 2, "S1"},
        {S4, 22, "S4"}
    };
    HeuristicFunc funcs[3];
    int failures = 0;
    size_t i;
    size_t k;

    funcs[0] = h_misplaced_tiles;
    funcs[1] = h_manhattan_distance;
    funcs[2] = h_zero;

    report_check(!is_valid_state(DUPLICATE) && !is_valid_state(NULL),
                 "非法输入被拒绝", &failures);
    report_check(is_solvable(S1, GOAL) && !is_solvable(UNSOLVABLE, GOAL),
                 "可解性判定与逆序数一致", &failures);

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int lengths[3];
        char description[96];
        int path_valid = 1;
        int lower_bound = 1;
        int goal_h_zero = 1;

        for (k = 0; k < 3; k++) {
            Pool* pool = create_pool(0);
            SearchMetrics metrics;
            Node* goal = NULL;

            if (pool != NULL) {
                goal = astar_solve(cases[i].state, GOAL, funcs[k], &metrics, pool);
            }
            lengths[k] = (goal != NULL) ? metrics.path_len : -1;
            if (goal == NULL || metrics.path_len != cases[i].expected_length) {
                path_valid = 0;
            } else {
                if (!path_is_valid(goal, cases[i].state, GOAL)) {
                    path_valid = 0;
                }
                if (metrics.path_len < h_manhattan_distance(cases[i].state, GOAL)) {
                    lower_bound = 0;
                }
                if (goal->h != 0) {
                    goal_h_zero = 0;
                }
            }
            free_pool(pool);
        }

        snprintf(description, sizeof(description), "%s：三策略长度一致且等于 %d 步",
                 cases[i].name, cases[i].expected_length);
        report_check(path_valid && lengths[0] == lengths[1] && lengths[1] == lengths[2],
                     description, &failures);
        snprintf(description, sizeof(description), "%s：解路径可逐步回放至目标", cases[i].name);
        report_check(path_valid, description, &failures);
        snprintf(description, sizeof(description), "%s：路径长度不小于 h2(start)", cases[i].name);
        report_check(lower_bound, description, &failures);
        snprintf(description, sizeof(description), "%s：目标节点 h = 0", cases[i].name);
        report_check(goal_h_zero, description, &failures);
    }

    {
        Pool* pool = create_pool(0);
        SearchMetrics metrics;
        Node* goal = NULL;

        if (pool != NULL) {
            goal = astar_solve(GOAL, GOAL, h_manhattan_distance, &metrics, pool);
        }
        report_check(goal != NULL && metrics.path_len == 0 && metrics.expanded == 0,
                     "初态即目标：路径长度 0 且不展开", &failures);
        free_pool(pool);
    }

    if (failures == 0) {
        printf("\n自检通过（%d 项断言全部成功）\n", 4 * 2 + 3);
        return EXIT_RC_OK;
    }
    printf("\n自检失败：%d 项断言未通过\n", failures);
    return EXIT_RC_SELFTEST;
}

int main(int argc, char* argv[])
{
    Config config;
    CliStatus status = parse_cli_args(argc, argv, &config);

    if (status == CLI_RC_HELP) {
        print_usage((argc > 0) ? argv[0] : NULL);
        return EXIT_RC_OK;
    }
    if (status != CLI_RC_OK) {
        return EXIT_RC_USAGE; /* 原因已由 cli 模块输出 */
    }
    if (config.selftest) {
        return run_selftest();
    }
    if (config.benchmark > 0) {
        return run_benchmark(&config);
    }
    return run_single(&config);
}
