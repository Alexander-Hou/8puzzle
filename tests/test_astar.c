/*
 * astar_solve 单元测试：最优性、路径合法性、指标口径与规模上界。
 *
 * 期望的最优步数由独立的 BFS 脚本核对过：
 *   S5 = 1，S1 = 2，S4 = 22，REV = 30，S3 = 31
 * 不可解实例（S2）的 BFS 会穷尽所在连通分量，恰好 181440 个状态。
 *
 * 运行：make test-astar
 */

#include "pool.h"
#include "puzzle.h"

#include <string.h>

#include "test_util.h"

static const int GOAL[3][3] = {
    {1, 2, 3},
    {4, 5, 6},
    {7, 8, 0}
};
static const int S1[3][3] = { /* 最优 2 步 */
    {1, 2, 3},
    {4, 0, 6},
    {7, 5, 8}
};
static const int S3[3][3] = { /* 难例，最优 31 步 */
    {8, 6, 7},
    {2, 5, 4},
    {3, 0, 1}
};
static const int S4[3][3] = { /* 最优 22 步 */
    {0, 1, 2},
    {3, 4, 5},
    {6, 7, 8}
};
static const int S5[3][3] = { /* 最优 1 步 */
    {1, 2, 3},
    {4, 5, 6},
    {7, 0, 8}
};
static const int REV[3][3] = { /* 最优 30 步 */
    {8, 7, 6},
    {5, 4, 3},
    {2, 1, 0}
};
static const int UNSOLVABLE[3][3] = { /* 逆序数为奇数，对标准目标无解 */
    {2, 8, 3},
    {1, 6, 4},
    {7, 0, 5}
};

typedef struct {
    Pool* pool;
    Node* goal;
    SearchMetrics metrics;
} Run;

static Run run_search(const int start[3][3], const int target[3][3], HeuristicFunc h_func)
{
    Run run;

    memset(&run, 0, sizeof(run));
    run.pool = create_pool(0);
    if (run.pool != NULL) {
        run.goal = astar_solve(start, target, h_func, &run.metrics, run.pool);
    }
    return run;
}

static void release_run(Run* run)
{
    free_pool(run->pool);
    run->pool = NULL;
    run->goal = NULL;
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

/* from -> to 是否为一次合法的空格移动（两格交换且相邻） */
static int is_legal_move(const int from[3][3], const int to[3][3])
{
    int from_row = -1;
    int from_col = -1;
    int to_row = -1;
    int to_col = -1;
    int diff = 0;
    int i;
    int j;
    int drow;
    int dcol;

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
    if (drow + dcol != 1) {
        return 0; /* 空格未移动到相邻格 */
    }
    return from[to_row][to_col] == to[from_row][from_col] && from[to_row][to_col] != 0;
}

static int parent_chain_length(const Node* node)
{
    int length = 0;

    while (node != NULL) {
        length++;
        node = node->parent;
    }
    return length;
}

/* 从目标沿 parent 回溯到起点，逐段校验合法性 */
static int path_is_valid(const Node* goal, const int start[3][3], const int target[3][3])
{
    const Node* chain[64];
    int length = 0;
    const Node* current = goal;
    int i;

    while (current != NULL) {
        if (length >= 64) {
            return 0;
        }
        chain[length++] = current;
        current = current->parent;
    }
    if (length == 0) {
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

static void test_optimal_length(void)
{
    static const struct {
        const int (*state)[3];
        int expected;
        const char* name;
    } cases[] = {
        {S5, 1, "S5"},
        {S1, 2, "S1"},
        {S4, 22, "S4"},
        {REV, 30, "REV"},
        {S3, 31, "S3"}
    };
    size_t i;

    begin_case("三种策略解路径长度一致且等于已知最优值");
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        HeuristicFunc funcs[3];
        size_t k;

        funcs[0] = h_misplaced_tiles;
        funcs[1] = h_manhattan_distance;
        funcs[2] = h_zero;
        for (k = 0; k < 3; k++) {
            Run run = run_search(cases[i].state, GOAL, funcs[k]);

            CHECK(run.goal != NULL);
            if (run.goal != NULL) {
                CHECK(run.metrics.path_len == cases[i].expected);
                CHECK(run.goal->g == cases[i].expected);
                CHECK(run.goal->h == 0);
                CHECK(run.goal->f == run.goal->g);
                CHECK(run.metrics.path_len >= h_manhattan_distance(cases[i].state, GOAL));
            }
            release_run(&run);
        }
    }
    end_case();
}

static void test_path_replay(void)
{
    Run run = run_search(S4, GOAL, h_manhattan_distance);

    begin_case("解路径可回放");
    CHECK(run.goal != NULL);
    if (run.goal != NULL) {
        CHECK(path_is_valid(run.goal, S4, GOAL));
        CHECK(parent_chain_length(run.goal) == run.metrics.path_len + 1);
    }
    release_run(&run);
    end_case();
}

static void test_metrics_consistency(void)
{
    Run run = run_search(S4, GOAL, h_manhattan_distance);

    begin_case("指标口径自洽");
    CHECK(run.goal != NULL);
    CHECK(run.metrics.expanded > 0);
    CHECK(run.metrics.generated > 0);
    CHECK(run.metrics.peak_open >= 1);
    CHECK(run.metrics.peak_closed != 0 || run.metrics.expanded == 0);
    CHECK(run.metrics.peak_closed == run.metrics.expanded);
    CHECK(run.metrics.total_alloc_nodes <= run.metrics.generated);
    CHECK(run.metrics.total_alloc_nodes > 0);
    CHECK(run.metrics.time_ms >= 0.0);
    release_run(&run);
    end_case();
}

static void test_trivial_case(void)
{
    Run run = run_search(GOAL, GOAL, h_manhattan_distance);

    begin_case("初态即目标");
    CHECK(run.goal != NULL);
    if (run.goal != NULL) {
        CHECK(run.metrics.path_len == 0);
        CHECK(run.goal->parent == NULL);
        CHECK(run.metrics.expanded == 0);
        CHECK(run.metrics.peak_closed == 0);
        CHECK(run.metrics.generated == 1);
        CHECK(run.metrics.total_alloc_nodes == 1);
        CHECK(run.metrics.peak_open == 1);
    }
    release_run(&run);
    end_case();
}

static void test_pruning_trend(void)
{
    Run bfs = run_search(S4, GOAL, h_zero);
    Run h1 = run_search(S4, GOAL, h_misplaced_tiles);
    Run h2 = run_search(S4, GOAL, h_manhattan_distance);

    begin_case("启发式剪枝趋势 bfs > h1 > h2");
    printf("  S4 expanded: bfs=%ld h1=%ld h2=%ld\n",
           bfs.metrics.expanded, h1.metrics.expanded, h2.metrics.expanded);
    CHECK(bfs.goal != NULL && h1.goal != NULL && h2.goal != NULL);
    CHECK(bfs.metrics.expanded > h1.metrics.expanded);
    CHECK(h1.metrics.expanded > h2.metrics.expanded);
    CHECK(bfs.metrics.generated > h2.metrics.generated);
    CHECK(bfs.metrics.path_len == h1.metrics.path_len);
    CHECK(h1.metrics.path_len == h2.metrics.path_len);
    release_run(&bfs);
    release_run(&h1);
    release_run(&h2);
    end_case();
}

static void test_unsolvable_exhausts_component(void)
{
    Run run = run_search(UNSOLVABLE, GOAL, h_zero);

    begin_case("不可解实例穷尽连通分量（上界 181440）");
    CHECK(run.goal == NULL);
    CHECK(run.metrics.expanded == 181440L);
    CHECK(run.metrics.peak_closed == 181440L);
    CHECK(run.metrics.expanded <= 181440L);
    release_run(&run);
    end_case();
}

static void test_determinism(void)
{
    Run first = run_search(S1, GOAL, h_manhattan_distance);
    Run second = run_search(S1, GOAL, h_manhattan_distance);

    begin_case("同一输入两次运行指标一致");
    CHECK(first.goal != NULL && second.goal != NULL);
    CHECK(first.metrics.path_len == second.metrics.path_len);
    CHECK(first.metrics.generated == second.metrics.generated);
    CHECK(first.metrics.expanded == second.metrics.expanded);
    CHECK(first.metrics.peak_open == second.metrics.peak_open);
    CHECK(first.metrics.peak_closed == second.metrics.peak_closed);
    CHECK(first.metrics.total_alloc_nodes == second.metrics.total_alloc_nodes);
    release_run(&first);
    release_run(&second);
    end_case();
}

int main(void)
{
    test_optimal_length();
    test_path_replay();
    test_metrics_consistency();
    test_trivial_case();
    test_pruning_trend();
    test_unsolvable_exhausts_component();
    test_determinism();
    return test_summary();
}
