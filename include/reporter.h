#ifndef REPORTER_H
#define REPORTER_H

/*
 * 结果展示模块：棋盘渲染、解路径回溯打印、性能指标报告、多策略对比表与 CSV 输出。
 * 本模块不参与搜索，只读取搜索产出（目标节点与 SearchMetrics）。
 */

#include <stdio.h>

#include "node.h"
#include "puzzle.h"

/* 按 3x3 格局打印棋盘（空格打印为 0） */
void print_board(const int state[3][3]);

/*
 * 从目标节点沿 parent 回溯并顺推打印完整解路径。
 * 每个移动行以固定前缀 "Step " 起始，便于自动化统计步数。
 */
void print_solution_path(const Node* goal_node);

/*
 * 打印单策略指标。min/max 为 NULL 时表示只运行了一次，不打印区间；
 * repeat > 1 时由调用方传入各指标的最小/最大值。
 */
void print_metrics_report(const SearchMetrics* median, const SearchMetrics* min,
                          const SearchMetrics* max, const char* method_name);

/* 打印多策略对比表；metrics 与 names 长度均为 count，顺序为 h1、h2、bfs */
void print_comparison_table(const SearchMetrics metrics[], const char* names[], int count);

/* CSV 表头（固定格式，见 docs/01-requirements.md 3.1.1） */
void write_csv_header(FILE* fp);

/*
 * 写一行 CSV。pruning_rate 为百分比数值（如 92.35 表示 92.35%）；
 * 传 NAN 表示"不适用"，该列留空（单策略模式）。
 * fp 或 metrics 为 NULL 时不做任何事。
 */
void write_csv_row(FILE* fp, const int start[3][3], const int target[3][3],
                   const char* method_name, const SearchMetrics* metrics, double pruning_rate);

#endif /* REPORTER_H */
