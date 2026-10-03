/*
 * pool 模块（Node 内存池）单元测试。
 * 运行：make test-pool
 */

#include "pool.h"

#include <stddef.h>
#include <stdint.h>

#include "test_util.h"

static void test_default_chunk(void)
{
    Pool* pool = create_pool(0);

    begin_case("create_pool 默认块大小");
    CHECK(pool != NULL);
    CHECK(pool->chunk_nodes == POOL_DEFAULT_CHUNK_NODES);
    CHECK(pool_total_allocated(pool) == 0);
    free_pool(pool);
    end_case();
}

static void test_alloc_returns_zeroed(void)
{
    Pool* pool = create_pool(4);
    Node* node;

    begin_case("pool_alloc 返回清零节点");
    CHECK(pool != NULL);
    node = pool_alloc(pool);
    CHECK(node != NULL);
    if (node != NULL) {
        CHECK(node->state[0][0] == 0 && node->state[2][2] == 0);
        CHECK(node->code == 0u);
        CHECK(node->g == 0 && node->h == 0 && node->f == 0);
        CHECK(node->zero_x == 0 && node->zero_y == 0);
        CHECK(node->parent == NULL);
    }
    CHECK(pool_total_allocated(pool) == 1);
    free_pool(pool);
    end_case();
}

static void test_chunk_growth(void)
{
    Pool* pool = create_pool(4); /* 故意小于需求，强制跨块 */
    Node* nodes[10];
    int distinct = 1;
    int i;
    int j;

    begin_case("跨块分配与地址唯一");
    CHECK(pool != NULL);
    for (i = 0; i < 10; i++) {
        nodes[i] = pool_alloc(pool);
        CHECK(nodes[i] != NULL);
    }
    for (i = 0; i < 10; i++) {
        for (j = i + 1; j < 10; j++) {
            if (nodes[i] == nodes[j]) {
                distinct = 0;
            }
        }
    }
    CHECK(distinct);
    CHECK(pool_total_allocated(pool) == 10);

    /* 写入互不干扰（块内无重叠） */
    for (i = 0; i < 10; i++) {
        nodes[i]->g = i;
    }
    for (i = 0; i < 10; i++) {
        CHECK(nodes[i]->g == i);
    }
    free_pool(pool);
    end_case();
}

static void test_null_and_overflow(void)
{
    begin_case("NULL 与溢出保护");
    CHECK(create_pool(SIZE_MAX) == NULL);
    CHECK(pool_alloc(NULL) == NULL);
    CHECK(pool_total_allocated(NULL) == 0);
    free_pool(NULL); /* 不应崩溃 */
    CHECK(1);
    end_case();
}

int main(void)
{
    test_default_chunk();
    test_alloc_returns_zeroed();
    test_chunk_growth();
    test_null_and_overflow();
    return test_summary();
}
