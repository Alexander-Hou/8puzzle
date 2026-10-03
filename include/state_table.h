#ifndef STATE_TABLE_H
#define STATE_TABLE_H

/*
 * 已发现状态表：用一个哈希表统一覆盖 OPEN 与 CLOSED。
 *
 * 每个状态只保留一条记录，记录已知最优 g、取得该 g 的规范节点与是否已关闭，
 * 从而支持"生成时比较 best_g、弹出时惰性跳过过期条目"的搜索主循环。
 *
 * 表不拥有 Node 内存：node 只是引用，节点由 pool 统一释放。
 */

#include <stdbool.h>

#include "node.h"

typedef struct StateEntry {
    unsigned int key;         /* base-9 状态编码 */
    int          best_g;      /* 已知到达该状态的最小 g */
    Node*        node;        /* 规范节点，用于路径回溯与目标输出 */
    bool         closed;      /* 是否已展开 */
    struct StateEntry* next;  /* 冲突链 */
} StateEntry;

typedef struct {
    StateEntry** buckets;
    int bucket_count;   /* 2 的幂 */
    long count;         /* 当前条目数 */
} StateTable;

/* 分配失败返回 NULL */
StateTable* create_state_table(void);

/* 未找到或参数为 NULL 时返回 NULL */
StateEntry* table_find(StateTable* table, unsigned int key);

/*
 * 追加一条新记录（调用方需先用 table_find 确认 key 不存在）。
 * 返回 false 表示分配失败。
 */
bool table_insert(StateTable* table, unsigned int key, int g, Node* node);

/* 用更优的 g 与规范节点刷新已有记录 */
void table_update(StateEntry* entry, int g, Node* node);

/* table 为 NULL 时返回 0 */
long table_count(const StateTable* table);

/* 释放所有条目与桶数组，不触碰 Node；允许传 NULL */
void free_state_table(StateTable* table);

#endif /* STATE_TABLE_H */
