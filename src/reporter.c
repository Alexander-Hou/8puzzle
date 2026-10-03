#include "reporter.h"

#include <stdio.h>

/* 单条解路径最长 31 步，节点数最多 32；留出余量 */
#define PATH_CHAIN_CAPACITY 64

void print_board(const int state[3][3])
{
    int i;
    int j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            printf("%s%d", (j == 0) ? "" : " ", state[i][j]);
        }
        printf("\n");
    }
}

/* 被移动数码的移动方向（与空格移动方向相反） */
static const char* move_direction(const Node* from, const Node* to)
{
    int drow = to->zero_x - from->zero_x;
    int dcol = to->zero_y - from->zero_y;

    if (drow < 0) {
        return "DOWN";
    }
    if (drow > 0) {
        return "UP";
    }
    if (dcol < 0) {
        return "RIGHT";
    }
    if (dcol > 0) {
        return "LEFT";
    }
    return "?";
}

void print_solution_path(const Node* goal_node)
{
    const Node* chain[PATH_CHAIN_CAPACITY];
    int length = 0;
    const Node* current = goal_node;
    int i;

    if (goal_node == NULL) {
        return;
    }
    while (current != NULL && length < PATH_CHAIN_CAPACITY) {
        chain[length++] = current;
        current = current->parent;
    }
    if (length == 0) {
        return;
    }

    printf("初始状态:\n");
    print_board(chain[length - 1]->state);
    printf("\n");

    for (i = length - 1; i > 0; i--) {
        const Node* from = chain[i];
        const Node* to = chain[i - 1];
        int tile = to->state[from->zero_x][from->zero_y];

        printf("Step %d: Move %d %s\n", length - i, tile, move_direction(from, to));
        print_board(to->state);
        printf("\n");
    }
}

void print_metrics_report(const SearchMetrics* median, const SearchMetrics* min,
                          const SearchMetrics* max, const char* method_name)
{
    int with_range;

    if (median == NULL) {
        return;
    }
    with_range = (min != NULL && max != NULL);

    if (with_range) {
        printf("策略 %s（重复运行取中位数）:\n", method_name);
    } else {
        printf("策略 %s:\n", method_name);
    }
    printf("  解路径长度          : %d\n", median->path_len);

    if (with_range) {
        printf("  生成节点数          : %ld  [%ld, %ld]\n",
               median->generated, min->generated, max->generated);
        printf("  扩展节点数          : %ld  [%ld, %ld]\n",
               median->expanded, min->expanded, max->expanded);
        printf("  OPEN 峰值           : %ld  [%ld, %ld]\n",
               median->peak_open, min->peak_open, max->peak_open);
        printf("  CLOSED 峰值         : %ld  [%ld, %ld]\n",
               median->peak_closed, min->peak_closed, max->peak_closed);
        printf("  累计分配节点数       : %ld  [%ld, %ld]\n",
               median->total_alloc_nodes, min->total_alloc_nodes, max->total_alloc_nodes);
        printf("  搜索耗时(ms)        : %.3f  [%.3f, %.3f]\n",
               median->time_ms, min->time_ms, max->time_ms);
    } else {
        printf("  生成节点数          : %ld\n", median->generated);
        printf("  扩展节点数          : %ld\n", median->expanded);
        printf("  OPEN 峰值           : %ld\n", median->peak_open);
        printf("  CLOSED 峰值         : %ld\n", median->peak_closed);
        printf("  累计分配节点数       : %ld\n", median->total_alloc_nodes);
        printf("  搜索耗时(ms)        : %.3f\n", median->time_ms);
    }
}

void print_comparison_table(const SearchMetrics metrics[], const char* names[], int count)
{
    int i;

    if (metrics == NULL || names == NULL || count <= 0) {
        return;
    }
    printf("%-5s %-9s %-11s %-11s %-10s %-11s %-10s %s\n",
           "策略", "路径长度", "生成节点", "扩展节点", "OPEN峰值", "CLOSED峰值", "耗时(ms)", "剪枝率");
    for (i = 0; i < count; i++) {
        double pruning;

        /* 以 BFS 作为基准：generated(bfs) 为分母；BFS 自身剪枝率为 0 */
        pruning = 1.0 - (double)metrics[i].generated / (double)metrics[count - 1].generated;
        printf("%-5s %-9d %-11ld %-11ld %-10ld %-11ld %-10.3f %.2f%%\n",
               names[i], metrics[i].path_len, metrics[i].generated, metrics[i].expanded,
               metrics[i].peak_open, metrics[i].peak_closed, metrics[i].time_ms,
               pruning * 100.0);
    }
}

void write_csv_header(FILE* fp)
{
    if (fp == NULL) {
        return;
    }
    fprintf(fp,
            "method,start,target,path_len,generated,expanded,peak_open,peak_closed,"
            "total_alloc_nodes,time_ms,pruning_rate\n");
}

static void state_to_text(const int state[3][3], char* buffer, size_t size)
{
    snprintf(buffer, size, "%d %d %d %d %d %d %d %d %d",
             state[0][0], state[0][1], state[0][2],
             state[1][0], state[1][1], state[1][2],
             state[2][0], state[2][1], state[2][2]);
}

void write_csv_row(FILE* fp, const int start[3][3], const int target[3][3],
                   const char* method_name, const SearchMetrics* metrics, double pruning_rate)
{
    char start_text[32];
    char target_text[32];

    if (fp == NULL || metrics == NULL) {
        return;
    }
    state_to_text(start, start_text, sizeof(start_text));
    state_to_text(target, target_text, sizeof(target_text));

    fprintf(fp, "%s,%s,%s,%d,%ld,%ld,%ld,%ld,%ld,%.3f,",
            method_name, start_text, target_text, metrics->path_len,
            metrics->generated, metrics->expanded, metrics->peak_open,
            metrics->peak_closed, metrics->total_alloc_nodes, metrics->time_ms);

    if (pruning_rate != pruning_rate) {
        fputc('\n', fp); /* NAN：该列留空（单策略模式无基准） */
    } else {
        fprintf(fp, "%.2f\n", pruning_rate);
    }
}
