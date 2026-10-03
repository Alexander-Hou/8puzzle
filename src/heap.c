#include "heap.h"

#include <stdlib.h>

int compare_nodes(const Node* a, const Node* b)
{
    if (a->f != b->f) {
        return (a->f < b->f) ? -1 : 1;
    }
    if (a->g != b->g) {
        return (a->g > b->g) ? -1 : 1; /* f 相同时优先更深（g 更大）的节点 */
    }
    if (a->h != b->h) {
        return (a->h < b->h) ? -1 : 1;
    }
    if (a->code != b->code) {
        return (a->code < b->code) ? -1 : 1;
    }
    return 0;
}

MinHeap* create_min_heap(int initial_capacity)
{
    MinHeap* heap;

    if (initial_capacity <= 0) {
        initial_capacity = HEAP_DEFAULT_CAPACITY;
    }
    heap = (MinHeap*)malloc(sizeof(MinHeap));
    if (heap == NULL) {
        return NULL;
    }
    heap->data = (Node**)malloc(sizeof(Node*) * (size_t)initial_capacity);
    if (heap->data == NULL) {
        free(heap);
        return NULL;
    }
    heap->capacity = initial_capacity;
    heap->size = 0;
    return heap;
}

int heap_push(MinHeap* heap, Node* node)
{
    int i;

    if (heap == NULL || node == NULL) {
        return -1;
    }
    if (heap->size >= heap->capacity) {
        int new_capacity = heap->capacity * 2;
        Node** grown;

        if (new_capacity <= heap->capacity) {
            return -1; /* 容量溢出 */
        }
        grown = (Node**)realloc(heap->data, sizeof(Node*) * (size_t)new_capacity);
        if (grown == NULL) {
            return -1;
        }
        heap->data = grown;
        heap->capacity = new_capacity;
    }

    i = heap->size++;
    while (i > 0) {
        int parent = (i - 1) / 2;

        if (compare_nodes(node, heap->data[parent]) >= 0) {
            break;
        }
        heap->data[i] = heap->data[parent];
        i = parent;
    }
    heap->data[i] = node;
    return 0;
}

Node* heap_pop(MinHeap* heap)
{
    Node* smallest;
    Node* last;
    int i;

    if (heap == NULL || heap->size == 0) {
        return NULL;
    }
    smallest = heap->data[0];
    last = heap->data[--heap->size];

    i = 0;
    while (i * 2 + 1 < heap->size) {
        int left = i * 2 + 1;
        int right = i * 2 + 2;
        int best = left;

        if (right < heap->size && compare_nodes(heap->data[right], heap->data[left]) < 0) {
            best = right;
        }
        if (compare_nodes(last, heap->data[best]) <= 0) {
            break;
        }
        heap->data[i] = heap->data[best];
        i = best;
    }
    heap->data[i] = last;
    return smallest;
}

int heap_size(const MinHeap* heap)
{
    return (heap != NULL) ? heap->size : 0;
}

void free_min_heap(MinHeap* heap)
{
    if (heap == NULL) {
        return;
    }
    free(heap->data);
    free(heap);
}
