/*
 * heap 模块（OPEN 表小顶堆）单元测试。
 * 运行：make test-heap
 */

#include "heap.h"

#include <string.h>

#include "test_util.h"

static Node g_nodes[512];
static int g_node_count = 0;

/* 从静态数组取节点，测试期间无需 malloc */
static Node* new_node(int f, int g, int h, unsigned int code)
{
    Node* node = &g_nodes[g_node_count++];

    memset(node, 0, sizeof(*node));
    node->f = f;
    node->g = g;
    node->h = h;
    node->code = code;
    return node;
}

static void reset_nodes(void)
{
    g_node_count = 0;
}

static void test_compare_order(void)
{
    reset_nodes();
    Node* a = new_node(5, 2, 3, 10);
    Node* b = new_node(5, 3, 2, 20); /* 同 f，g 更大 -> 优先 */
    Node* c = new_node(5, 3, 2, 5);  /* 同 f,g,h，编码更小 -> 优先 */
    Node* d = new_node(5, 3, 2, 5);  /* 完全相同 -> 0 */
    Node* e = new_node(4, 9, 0, 99); /* f 更小 -> 最优先 */

    begin_case("compare_nodes 确定性全序");
    CHECK(compare_nodes(a, b) > 0);
    CHECK(compare_nodes(b, a) < 0);
    CHECK(compare_nodes(b, c) > 0);
    CHECK(compare_nodes(c, b) < 0);
    CHECK(compare_nodes(c, d) == 0);
    CHECK(compare_nodes(e, a) < 0);
    end_case();
}

static void test_pop_order(void)
{
    static const struct { int f, g, h; } input[] = {
        {5, 2, 3}, {3, 1, 2}, {7, 3, 4}, {1, 0, 1}, {3, 2, 1}
    };
    MinHeap* heap = create_min_heap(1);
    int last_f = -1;
    size_t i;

    reset_nodes();
    begin_case("按 f 升序弹出");
    CHECK(heap != NULL);
    for (i = 0; i < sizeof(input) / sizeof(input[0]); i++) {
        CHECK(heap_push(heap, new_node(input[i].f, input[i].g, input[i].h, (unsigned int)i)) == 0);
    }
    CHECK(heap_size(heap) == 5);
    for (i = 0; i < 5; i++) {
        Node* node = heap_pop(heap);

        CHECK(node != NULL);
        if (node != NULL) {
            CHECK(node->f >= last_f);
            last_f = node->f;
        }
    }
    CHECK(heap_pop(heap) == NULL);
    CHECK(heap_size(heap) == 0);
    free_min_heap(heap);
    end_case();
}

static void test_tie_break(void)
{
    MinHeap* heap = create_min_heap(4);
    Node* shallow = new_node(9, 1, 8, 100);
    Node* deep = new_node(9, 5, 4, 200);
    int ok;

    begin_case("f 相同时优先 g 更大");
    CHECK(heap != NULL);
    CHECK(heap_push(heap, shallow) == 0);
    CHECK(heap_push(heap, deep) == 0);
    ok = (heap_pop(heap) == deep);
    CHECK(ok);
    ok = (heap_pop(heap) == shallow);
    CHECK(ok);
    free_min_heap(heap);
    end_case();
}

static void test_growth_and_determinism(void)
{
    MinHeap* h1 = create_min_heap(1);
    MinHeap* h2 = create_min_heap(1);
    int order1[200];
    int order2[200];
    int count1 = 0;
    int count2 = 0;
    int non_decreasing = 1;
    int same = 1;
    int i;

    reset_nodes();
    for (i = 0; i < 200; i++) {
        /* f 与编码都按 i 递增，保证比较器可完全定序 */
        CHECK(heap_push(h1, new_node(i % 17, i % 17, 0, (unsigned int)i)) == 0);
    }
    for (i = 199; i >= 0; i--) {
        CHECK(heap_push(h2, new_node(i % 17, i % 17, 0, (unsigned int)i)) == 0);
    }

    begin_case("容量自动扩容与插入顺序无关");
    CHECK(heap_size(h1) == 200);
    CHECK(heap_size(h2) == 200);
    while (heap_size(h1) > 0) {
        order1[count1++] = heap_pop(h1)->f;
    }
    while (heap_size(h2) > 0) {
        order2[count2++] = heap_pop(h2)->f;
    }
    for (i = 1; i < count1; i++) {
        if (order1[i] < order1[i - 1]) {
            non_decreasing = 0;
        }
    }
    CHECK(non_decreasing);
    CHECK(count1 == 200 && count2 == 200);
    for (i = 0; i < 200; i++) {
        if (order1[i] != order2[i]) {
            same = 0;
        }
    }
    CHECK(same);
    free_min_heap(h1);
    free_min_heap(h2);
    end_case();
}

static void test_null_handling(void)
{
    MinHeap* heap = create_min_heap(0);
    Node* node = new_node(1, 0, 1, 1);

    begin_case("NULL 与非法参数");
    CHECK(heap != NULL);
    CHECK(heap_size(NULL) == 0);
    CHECK(heap_pop(NULL) == NULL);
    CHECK(heap_push(NULL, node) == -1);
    CHECK(heap_push(heap, NULL) == -1);
    free_min_heap(NULL); /* 不应崩溃 */
    free_min_heap(heap);
    CHECK(1);
    end_case();
}

int main(void)
{
    test_compare_order();
    test_pop_order();
    test_tie_break();
    test_growth_and_determinism();
    test_null_handling();
    return test_summary();
}
