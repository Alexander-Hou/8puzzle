#ifndef POOL_H
#define POOL_H

/*
 * Node 内存池（arena）。
 *
 * 按块批量分配，搜索结束后一次性释放，避免逐节点 free 带来的泄漏与 double free。
 */

#include <stddef.h>

#include "node.h"

#define POOL_DEFAULT_CHUNK_NODES 8192u

typedef struct PoolChunk {
    struct PoolChunk* next;
    size_t used;          /* 本块已使用的节点数 */
    Node nodes[];         /* 柔性数组，长度由 Pool.chunk_nodes 决定 */
} PoolChunk;

typedef struct {
    PoolChunk* head;          /* 块链表 */
    size_t chunk_nodes;       /* 每块可容纳的节点数 */
    long total_allocated;     /* 累计分配的节点数 */
} Pool;

/* chunk_nodes 为 0 时使用 POOL_DEFAULT_CHUNK_NODES；参数过大或分配失败返回 NULL */
Pool* create_pool(size_t chunk_nodes);

/* 返回已清零的节点；失败返回 NULL */
Node* pool_alloc(Pool* pool);

/* pool 为 NULL 时返回 0 */
long pool_total_allocated(const Pool* pool);

/* 一次性释放所有块；允许传 NULL */
void free_pool(Pool* pool);

#endif /* POOL_H */
