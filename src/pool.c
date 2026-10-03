#include "pool.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* 判断 chunk_nodes 是否会令 sizeof(PoolChunk) + chunk_nodes * sizeof(Node) 溢出 */
static int chunk_size_overflows(size_t chunk_nodes)
{
    return chunk_nodes > (SIZE_MAX - sizeof(PoolChunk)) / sizeof(Node);
}

static PoolChunk* new_chunk(size_t chunk_nodes)
{
    size_t bytes;
    PoolChunk* chunk;

    if (chunk_size_overflows(chunk_nodes)) {
        return NULL;
    }
    bytes = sizeof(PoolChunk) + chunk_nodes * sizeof(Node);
    chunk = (PoolChunk*)malloc(bytes);
    if (chunk == NULL) {
        return NULL;
    }
    chunk->next = NULL;
    chunk->used = 0;
    return chunk;
}

Pool* create_pool(size_t chunk_nodes)
{
    Pool* pool;

    if (chunk_nodes == 0) {
        chunk_nodes = POOL_DEFAULT_CHUNK_NODES;
    }
    if (chunk_size_overflows(chunk_nodes)) {
        return NULL;
    }
    pool = (Pool*)malloc(sizeof(Pool));
    if (pool == NULL) {
        return NULL;
    }
    pool->head = NULL;
    pool->chunk_nodes = chunk_nodes;
    pool->total_allocated = 0;
    return pool;
}

Node* pool_alloc(Pool* pool)
{
    PoolChunk* chunk;
    Node* node;

    if (pool == NULL) {
        return NULL;
    }
    chunk = pool->head;
    if (chunk == NULL || chunk->used == pool->chunk_nodes) {
        PoolChunk* fresh = new_chunk(pool->chunk_nodes);

        if (fresh == NULL) {
            return NULL;
        }
        fresh->next = pool->head;
        pool->head = fresh;
        chunk = fresh;
    }
    node = &chunk->nodes[chunk->used++];
    memset(node, 0, sizeof(*node));
    pool->total_allocated++;
    return node;
}

long pool_total_allocated(const Pool* pool)
{
    return (pool != NULL) ? pool->total_allocated : 0;
}

void free_pool(Pool* pool)
{
    PoolChunk* chunk;

    if (pool == NULL) {
        return;
    }
    chunk = pool->head;
    while (chunk != NULL) {
        PoolChunk* next = chunk->next;

        free(chunk);
        chunk = next;
    }
    free(pool);
}
