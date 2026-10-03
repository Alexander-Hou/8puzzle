/*
 * state_table 模块（已发现状态表）单元测试。
 * 运行：make test-state-table
 */

#include "state_table.h"

#include <string.h>

#include "test_util.h"

static Node g_dummy;

static void test_empty(void)
{
    StateTable* table = create_state_table();

    begin_case("空表查找与计数");
    CHECK(table != NULL);
    CHECK(table_count(table) == 0);
    CHECK(table_find(table, 12345u) == NULL);
    free_state_table(table);
    end_case();
}

static void test_insert_find_update(void)
{
    StateTable* table = create_state_table();
    Node first;
    Node second;
    StateEntry* entry;

    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));

    begin_case("插入 / 查找 / 更新");
    CHECK(table != NULL);
    CHECK(table_insert(table, 100u, 0, &first));
    CHECK(table_insert(table, 200u, 5, &first));
    CHECK(table_count(table) == 2);

    entry = table_find(table, 100u);
    CHECK(entry != NULL);
    if (entry != NULL) {
        CHECK(entry->key == 100u);
        CHECK(entry->best_g == 0);
        CHECK(entry->node == &first);
        CHECK(!entry->closed);
        entry->closed = true;
        CHECK(table_find(table, 100u)->closed);
    }

    entry = table_find(table, 200u);
    CHECK(entry != NULL);
    if (entry != NULL) {
        table_update(entry, 3, &second);
        CHECK(entry->best_g == 3);
        CHECK(entry->node == &second);
    }
    CHECK(table_find(table, 200u) != NULL && table_find(table, 200u)->best_g == 3);
    CHECK(table_find(table, 999u) == NULL);
    CHECK(table_count(table) == 2); /* 更新不增加条目 */
    free_state_table(table);
    end_case();
}

static void test_bulk_collisions(void)
{
    enum { N = 10000 };
    StateTable* table = create_state_table();
    int inserted = 1;
    int found = 1;
    int i;

    begin_case("批量插入与查找（含冲突链）");
    CHECK(table != NULL);
    for (i = 0; i < N; i++) {
        unsigned int key = (unsigned int)i * 2654435761u; /* 奇数乘法在 2^32 上为双射 */

        if (!table_insert(table, key, i % 7, &g_dummy)) {
            inserted = 0;
        }
    }
    CHECK(inserted);
    CHECK(table_count(table) == N);

    for (i = 0; i < N; i++) {
        unsigned int key = (unsigned int)i * 2654435761u;
        StateEntry* entry = table_find(table, key);

        if (entry == NULL || entry->key != key || entry->best_g != i % 7) {
            found = 0;
        }
    }
    CHECK(found);
    free_state_table(table);
    end_case();
}

static void test_null_handling(void)
{
    begin_case("NULL 参数");
    CHECK(table_find(NULL, 1u) == NULL);
    CHECK(!table_insert(NULL, 1u, 0, &g_dummy));
    CHECK(table_count(NULL) == 0);
    table_update(NULL, 0, NULL); /* 不应崩溃 */
    free_state_table(NULL);      /* 不应崩溃 */
    CHECK(1);
    end_case();
}

int main(void)
{
    test_empty();
    test_insert_find_update();
    test_bulk_collisions();
    test_null_handling();
    return test_summary();
}
