#ifndef HEAP_H
#define HEAP_H

/*
 * OPEN 表：基于数组的二叉小顶堆，存放 Node 指针。
 * 堆不拥有节点内存（节点由 pool 统一管理）。
 */

#include "node.h"

#define HEAP_DEFAULT_CAPACITY 1024

typedef struct {
    Node** data;
    int capacity;
    int size;
} MinHeap;

/*
 * 确定性全序比较器（<0 表示 a 优先）：
 *   f 升序 -> g 降序 -> h 升序 -> 状态编码升序
 * 前两级是常规 tie-break；最后一级消除残余并列，保证同一输入多次运行结果一致。
 * 要求 a、b 非 NULL。
 */
int compare_nodes(const Node* a, const Node* b);

/* initial_capacity <= 0 时使用 HEAP_DEFAULT_CAPACITY；分配失败返回 NULL */
MinHeap* create_min_heap(int initial_capacity);

/* 入堆；0 成功，-1 失败（参数非法或扩容失败） */
int heap_push(MinHeap* heap, Node* node);

/* 弹出 f 最小的节点；空堆或参数为 NULL 时返回 NULL */
Node* heap_pop(MinHeap* heap);

/* heap 为 NULL 时返回 0 */
int heap_size(const MinHeap* heap);

/* 释放堆数组，不触碰 Node；允许传 NULL */
void free_min_heap(MinHeap* heap);

#endif /* HEAP_H */
