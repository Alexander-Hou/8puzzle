/* clock_gettime 需要 POSIX 声明，且必须在任何头文件之前定义 */
#define _POSIX_C_SOURCE 200809L

#include "puzzle.h"

#include <stddef.h>
#include <string.h>
#include <time.h>

#include "heap.h"
#include "state_table.h"

bool is_valid_state(const int state[3][3])
{
    unsigned int seen = 0;
    int i;
    int j;

    if (state == NULL) {
        return false;
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            int value = state[i][j];
            unsigned int bit;

            if (value < 0 || value > 8) {
                return false;
            }
            bit = 1u << (unsigned int)value;
            if ((seen & bit) != 0u) {
                return false;
            }
            seen |= bit;
        }
    }
    return true; /* 9 个互不相同且都在 0..8 内，必然取遍 0..8 */
}

int inversion_count(const int state[3][3])
{
    int sequence[9];
    int n = 0;
    int inversions = 0;
    int i;
    int j;

    if (state == NULL) {
        return -1;
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            if (state[i][j] != 0) {
                sequence[n++] = state[i][j];
            }
        }
    }
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (sequence[i] > sequence[j]) {
                inversions++;
            }
        }
    }
    return inversions;
}

bool is_solvable(const int start[3][3], const int target[3][3])
{
    if (!is_valid_state(start) || !is_valid_state(target)) {
        return false;
    }
    return ((inversion_count(start) & 1) == (inversion_count(target) & 1));
}

unsigned int state_encode(const int state[3][3])
{
    unsigned int code = 0;
    int i;
    int j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            code = code * 9u + (unsigned int)state[i][j];
        }
    }
    return code;
}

int h_misplaced_tiles(const int state[3][3], const int target[3][3])
{
    int target_index[9];
    int misplaced = 0;
    int i;
    int j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            target_index[target[i][j]] = i * 3 + j;
        }
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            int tile = state[i][j];

            if (tile != 0 && target_index[tile] != i * 3 + j) {
                misplaced++;
            }
        }
    }
    return misplaced;
}

int h_manhattan_distance(const int state[3][3], const int target[3][3])
{
    int target_row[9];
    int target_col[9];
    int distance = 0;
    int i;
    int j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            target_row[target[i][j]] = i;
            target_col[target[i][j]] = j;
        }
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            int tile = state[i][j];

            if (tile != 0) {
                int drow = i - target_row[tile];
                int dcol = j - target_col[tile];

                distance += (drow < 0 ? -drow : drow) + (dcol < 0 ? -dcol : dcol);
            }
        }
    }
    return distance;
}

int h_zero(const int state[3][3], const int target[3][3])
{
    (void)state;
    (void)target;
    return 0;
}

/* --------------------------------------------------------------- A* 搜索 */

/* 内部保护上限：超过即认为是实现异常，交由上层以退出码 3 报告 */
#define ASTAR_NODE_LIMIT 5000000L

/* 空格移动方向：上、下、左、右 */
static const int ASTAR_DR[4] = {-1, 1, 0, 0};
static const int ASTAR_DC[4] = {0, 0, -1, 1};

/* 从内存池创建节点并填充全部字段 */
static Node* make_node(Pool* pool, const int state[3][3], unsigned int code,
                       const int target[3][3], HeuristicFunc h_func,
                       int g, Node* parent)
{
    Node* node = pool_alloc(pool);
    int i;
    int j;

    if (node == NULL) {
        return NULL;
    }
    memcpy(node->state, state, sizeof(node->state));
    node->code = code;
    node->g = g;
    node->h = h_func(state, target);
    node->f = node->g + node->h;
    node->parent = parent;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            if (state[i][j] == 0) {
                node->zero_x = i;
                node->zero_y = j;
            }
        }
    }
    return node;
}

static double elapsed_ms(const struct timespec* start, const struct timespec* end)
{
    return (double)(end->tv_sec - start->tv_sec) * 1000.0
         + (double)(end->tv_nsec - start->tv_nsec) / 1.0e6;
}

Node* astar_solve(const int start[3][3], const int target[3][3],
                  HeuristicFunc h_func, SearchMetrics* metrics, Pool* pool)
{
    MinHeap* heap;
    StateTable* table;
    Node* root;
    Node* goal = NULL;
    unsigned int goal_code;
    long base_allocated;
    long generated = 0;
    long expanded = 0;
    long peak_open = 0;
    long peak_closed = 0;
    struct timespec t_start;
    struct timespec t_end;

    if (metrics != NULL) {
        memset(metrics, 0, sizeof(*metrics));
    }
    if (start == NULL || target == NULL || h_func == NULL || pool == NULL) {
        return NULL;
    }

    heap = create_min_heap(HEAP_DEFAULT_CAPACITY);
    table = create_state_table();
    if (heap == NULL || table == NULL) {
        free_min_heap(heap);
        free_state_table(table);
        return NULL;
    }

    base_allocated = pool_total_allocated(pool);
    goal_code = state_encode(target);
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    root = make_node(pool, start, state_encode(start), target, h_func, 0, NULL);
    if (root == NULL) {
        goto done;
    }
    generated = 1;
    if (!table_insert(table, root->code, root->g, root)) {
        goto done;
    }
    if (heap_push(heap, root) != 0) {
        goto done;
    }
    peak_open = heap_size(heap);

    for (;;) {
        Node* node = heap_pop(heap);
        StateEntry* entry;
        int direction;

        if (node == NULL) {
            break; /* OPEN 为空：可解输入下不应发生 */
        }
        entry = table_find(table, node->code);
        if (entry == NULL || entry->closed || entry->best_g != node->g) {
            continue; /* 过期或重复条目，惰性跳过 */
        }

        /* 目标判定放在弹出时：这是最优性的关键，且不计入 expanded */
        if (node->code == goal_code) {
            goal = node;
            break;
        }

        /*
         * 标记关闭。h1/h2 均一致，已关闭状态不可能再被更优的 g 到达，
         * 因此无需"重新打开"；若将来引入不一致启发式，此处需要改为可重新打开。
         */
        entry->closed = true;
        expanded++;
        if (expanded > peak_closed) {
            peak_closed = expanded;
        }

        for (direction = 0; direction < 4; direction++) {
            int next_state[3][3];
            const int(*next_view)[3];
            int row = node->zero_x + ASTAR_DR[direction];
            int col = node->zero_y + ASTAR_DC[direction];
            unsigned int code;
            StateEntry* found;
            Node* child;

            if (row < 0 || row > 2 || col < 0 || col > 2) {
                continue;
            }
            memcpy(next_state, node->state, sizeof(next_state));
            next_state[node->zero_x][node->zero_y] = next_state[row][col];
            next_state[row][col] = 0;
            /* C23 之前 int(*)[3] 不能隐式转换为 const int(*)[3]，显式加 const */
            next_view = (const int(*)[3])next_state;

            generated++;
            if (generated > ASTAR_NODE_LIMIT) {
                goto done;
            }

            code = state_encode(next_view);
            found = table_find(table, code);
            if (found != NULL && found->best_g <= node->g + 1) {
                continue; /* 已有不劣于该路径的记录：丢弃候选，不分配 Node */
            }

            child = make_node(pool, next_view, code, target, h_func, node->g + 1, node);
            if (child == NULL) {
                goto done;
            }
            if (found == NULL) {
                if (!table_insert(table, code, child->g, child)) {
                    goto done;
                }
            } else {
                table_update(found, child->g, child);
            }
            if (heap_push(heap, child) != 0) {
                goto done;
            }
            if (heap_size(heap) > peak_open) {
                peak_open = heap_size(heap);
            }
        }
    }

done:
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    if (metrics != NULL) {
        metrics->path_len = (goal != NULL) ? goal->g : 0;
        metrics->generated = generated;
        metrics->expanded = expanded;
        metrics->peak_open = peak_open;
        metrics->peak_closed = peak_closed;
        metrics->total_alloc_nodes = pool_total_allocated(pool) - base_allocated;
        metrics->time_ms = elapsed_ms(&t_start, &t_end);
    }
    free_min_heap(heap);
    free_state_table(table);
    return goal;
}
