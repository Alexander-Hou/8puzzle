#ifndef PUZZLE_H
#define PUZZLE_H

/*
 * 棋盘状态、启发函数与 A* 搜索核心。
 *
 * 职责：棋盘合法性判定、逆序数可解性判定、状态编码、三种估价函数 h1/h2/h0，
 * 以及基于"已发现状态表 + 惰性删除"的 A* 主循环。
 * 本模块不参与任何 I/O（进度与结果输出由 reporter 负责）。
 *
 * 约定：state/target 均为 3x3 数组，元素为 0..8，0 表示空格。
 * 除 is_valid_state / is_solvable / inversion_count 外，其余函数要求传入的
 * 棋盘是 0..8 的合法排列（由调用方或 cli 模块保证）。
 */

#include <stdbool.h>

#include "node.h"
#include "pool.h"

/*
 * 判断棋盘是否恰好由 0..8 各一次构成。state 为 NULL 时返回 false。
 *
 * 注：cli 模块的 parse_state_string 负责"字符串 -> 数组"的带诊断解析
 * （区分非数字 / 个数错误 / 越界 / 重复）；本函数是数组级谓词，供搜索与自检使用。
 */
bool is_valid_state(const int state[3][3]);

/*
 * 逆序数：按行优先展开后忽略空格 0，统计前面的数大于后面的数的数对个数。
 * state 为 NULL 时返回 -1（调用方需先确保棋盘合法，否则结果无意义）。
 */
int inversion_count(const int state[3][3]);

/*
 * 可解性判定：3x3 棋盘的宽度为奇数，空格所在行不影响奇偶性，
 * 因此"初始状态与目标状态的逆序数奇偶性相同" 等价于 有解。
 * 任一方非法（含 NULL）时返回 false。
 */
bool is_solvable(const int start[3][3], const int target[3][3]);

/*
 * 状态编码：按行优先把 9 个数字视为 9 位九进制数，取值 0..9^9-1。
 * 合法棋盘与编码一一对应，用于哈希与确定性 tie-break。
 */
unsigned int state_encode(const int state[3][3]);

/* h1：放错位置的数码个数（不含空格） */
int h_misplaced_tiles(const int state[3][3], const int target[3][3]);

/* h2：各数码到目标位置的曼哈顿距离之和（不含空格） */
int h_manhattan_distance(const int state[3][3], const int target[3][3]);

/* h0：恒为 0，使 A* 退化为逐层扩展的搜索，作为 BFS 基准 */
int h_zero(const int state[3][3], const int target[3][3]);

/* 策略模式使用的启发函数指针类型 */
typedef int (*HeuristicFunc)(const int state[3][3], const int target[3][3]);

/*
 * 单次搜索的统计指标。定义与 docs/01-requirements.md 3.1 节一致：
 *   path_len          解路径长度（目标节点的 g）；失败时为 0
 *   generated         扩展产生的候选子节点数（含被 best_g 比较丢弃的，不要求实际分配 Node）
 *   expanded          真正展开（关闭）的节点数；判定为目标的弹出不计入
 *   peak_open         堆内元素数量峰值（含尚未弹出的过期条目）
 *   peak_closed       关闭状态数峰值；由于关闭只增不减，等于 expanded
 *   total_alloc_nodes 本次搜索从内存池实际分配的 Node 数（<= generated）
 *   time_ms           搜索主循环耗时（毫秒，单调时钟）
 */
typedef struct {
    int    path_len;
    long   generated;
    long   expanded;
    long   peak_open;
    long   peak_closed;
    long   total_alloc_nodes;
    double time_ms;
} SearchMetrics;

/*
 * A* 搜索主循环（策略模式：启发函数由调用方注入）。
 *
 * 前置条件：start/target 为合法棋盘，且调用方已用 is_solvable 判为可解。
 * 返回值：目标节点指针（沿 parent 可回溯完整路径），节点由 pool 持有；
 *         返回 NULL 表示分配失败、超出节点数上限或内部异常（对应退出码 3）。
 */
Node* astar_solve(const int start[3][3], const int target[3][3],
                  HeuristicFunc h_func, SearchMetrics* metrics, Pool* pool);

#endif /* PUZZLE_H */
