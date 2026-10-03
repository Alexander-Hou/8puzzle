#include "state_table.h"

#include <stdlib.h>

/* 8 数码可达状态仅 181,440；2^19 桶的负载因子低于 0.35 */
#define TABLE_BUCKET_BITS  19
#define TABLE_BUCKET_COUNT (1 << TABLE_BUCKET_BITS)

/*
 * base-9 编码的低位由棋盘末尾格子决定，直接对 2 的幂取掩码会明显聚集；
 * 先做一次乘法混合再取低位，可打散分布且避免整数除法。
 */
static unsigned int mix_hash(unsigned int x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static StateEntry** bucket_of(StateTable* table, unsigned int key)
{
    unsigned int index = mix_hash(key) & (unsigned int)(table->bucket_count - 1);

    return &table->buckets[index];
}

static StateEntry* find_entry(StateTable* table, unsigned int key)
{
    StateEntry* entry;

    for (entry = *bucket_of(table, key); entry != NULL; entry = entry->next) {
        if (entry->key == key) {
            return entry;
        }
    }
    return NULL;
}

StateTable* create_state_table(void)
{
    StateTable* table = (StateTable*)malloc(sizeof(StateTable));

    if (table == NULL) {
        return NULL;
    }
    table->buckets = (StateEntry**)calloc((size_t)TABLE_BUCKET_COUNT, sizeof(StateEntry*));
    if (table->buckets == NULL) {
        free(table);
        return NULL;
    }
    table->bucket_count = TABLE_BUCKET_COUNT;
    table->count = 0;
    return table;
}

StateEntry* table_find(StateTable* table, unsigned int key)
{
    if (table == NULL) {
        return NULL;
    }
    return find_entry(table, key);
}

bool table_insert(StateTable* table, unsigned int key, int g, Node* node)
{
    StateEntry** head;
    StateEntry* entry;

    if (table == NULL) {
        return false;
    }
    entry = (StateEntry*)malloc(sizeof(StateEntry));
    if (entry == NULL) {
        return false;
    }
    head = bucket_of(table, key);
    entry->key = key;
    entry->best_g = g;
    entry->node = node;
    entry->closed = false;
    entry->next = *head;
    *head = entry;
    table->count++;
    return true;
}

void table_update(StateEntry* entry, int g, Node* node)
{
    if (entry == NULL) {
        return;
    }
    entry->best_g = g;
    entry->node = node;
}

long table_count(const StateTable* table)
{
    return (table != NULL) ? table->count : 0;
}

void free_state_table(StateTable* table)
{
    int i;

    if (table == NULL) {
        return;
    }
    for (i = 0; i < table->bucket_count; i++) {
        StateEntry* entry = table->buckets[i];

        while (entry != NULL) {
            StateEntry* next = entry->next;

            free(entry);
            entry = next;
        }
    }
    free(table->buckets);
    free(table);
}
