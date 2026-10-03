# 8 数码问题 A* 算法求解程序 —— 核心数据结构设计文档

## 1. 设计概述

在 8 数码问题的 A* 算法实现中，选择与设计高效的数据结构是确保程序运行性能、控制内存占用以及避免死循环的关键。本项目涉及四个核心数据结构：

1. **状态节点结构体 (`Node`)**：存储状态矩阵、路径代价 $g$、启发评估值 $h$ 及回溯父指针。
2. **小顶堆（Min-Heap / OPEN 表）**：按优先度 $f = g + h$ 维护优先队列，以 O(log N) 时间弹出最优节点。
3. **状态表（State Table）**：统一记录所有**已发现**状态（涵盖 OPEN 与 CLOSED），提供 `best_g`、规范节点指针与关闭标志，支撑去重与惰性删除。
4. **Node 内存池（Pool）**：按块批量分配 `Node`，程序结束一次性释放，保证内存安全。

---

## 2. 状态节点结构体 (`Node`)

### 2.1 结构体定义

```c
typedef struct Node {
    int state[3][3];       // 3x3 棋盘格局 (0 代表空格)
    unsigned int code;     // state 的 base-9 编码缓存，恒等于 state_encode(state)
    int zero_x;            // 空格 0 的行坐标 (0 ~ 2)
    int zero_y;            // 空格 0 的列坐标 (0 ~ 2)
    int g;                 // 起点到当前节点的实际代价 (移动步数)
    int h;                 // 当前节点到目标节点的估计代价
    int f;                 // f = g + h (综合优先级)
    struct Node* parent;   // 指向生成当前节点的父节点指针
} Node;
```

### 2.2 字段说明与设计考量

* `zero_x`, `zero_y`：缓存空格 `0` 的坐标，扩展邻居时无需每次重新扫描 3×3 矩阵。
* `code`：缓存 base-9 状态编码。状态表直接以它为键，堆比较器也用它做最后一级 tie-break，从而避免每次比较都重新编码，且让 heap 模块不必依赖 puzzle 模块（`Node` 定义在 `node.h` 中，被各搜索模块共享）。
* `f`：虽可由 `g + h` 推导，但缓存后堆比较无需重复加法，且便于调试；8 数码规模下额外 4 字节可忽略。
* `parent` 指针：用于顺推还原解路径。命中目标后从目标节点沿 `parent` 逆向回溯到起点即可得到完整步骤。
* **内存拷贝优化**：堆与状态表只传递 `Node*` 指针，避免深拷贝整个结构体。
* **状态表示选择**：保留 `int state[3][3]`（约 72 字节/节点）而非压缩为 `uint32_t`。8 数码累计分配节点通常在数十万量级，向量化带来的复杂度收益不成比例，"简单优先"。

---

## 3. OPEN 表 —— 小顶堆（Min-Heap）

### 3.1 抽象数据类型定义

```c
typedef struct {
    Node** data;     // 动态指针数组，存储指向 Node 的指针
    int capacity;    // 当前堆数组的最大容量
    int size;        // 当前堆中元素数量
} MinHeap;
```

堆只持有 `Node*`，**不拥有节点内存**；节点由内存池管理（见第 5 节）。

### 3.2 堆比较逻辑与确定性全序

堆调序依据 `compare_nodes`，构成严格全序，保证同一输入多次运行结果完全一致：

```c
int compare_nodes(const Node* a, const Node* b) {
    if (a->f != b->f) return (a->f < b->f) ? -1 : 1;        // 1) f 升序
    if (a->g != b->g) return (a->g > b->g) ? -1 : 1;        // 2) g 降序（优先深层）
    if (a->h != b->h) return (a->h < b->h) ? -1 : 1;        // 3) h 升序
    unsigned int ka = state_encode(a->state);
    unsigned int kb = state_encode(b->state);
    if (ka != kb) return (ka < kb) ? -1 : 1;                // 4) 状态编码升序
    return 0;
}
```

* 平局破局规则：`f` 相同时优先 `g` 更大（更接近目标）的节点。
* 最后一级状态编码比较消除残余并列，避免堆顺序随 `realloc`、分配地址等不确定因素变化。
* $h \equiv 0$（BFS）时 `f ≡ g`，规则自动退化为"层序 + 编码序"，与需求文档 2.4 的口径说明一致。

### 3.3 核心操作算法

#### 1. 入堆与上滤（`heap_push`）

```c
int heap_push(MinHeap* heap, Node* node) {
    if (heap->size >= heap->capacity) {
        int new_cap = heap->capacity * 2;
        Node** p = (Node**)realloc(heap->data, sizeof(Node*) * (size_t)new_cap);
        if (p == NULL) return -1;              // 由上层转为退出码 3
        heap->data = p;
        heap->capacity = new_cap;
    }
    int i = heap->size++;
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (compare_nodes(node, heap->data[parent]) >= 0) break;
        heap->data[i] = heap->data[parent];
        i = parent;
    }
    heap->data[i] = node;
    return 0;
}
```

时间复杂度 O(log N)；扩容失败返回非 0，不允许直接崩溃。

#### 2. 出堆与下滤（`heap_pop`）

```c
Node* heap_pop(MinHeap* heap) {
    if (heap->size == 0) return NULL;
    Node* min_node = heap->data[0];
    Node* last_node = heap->data[--heap->size];

    int i = 0;
    while (i * 2 + 1 < heap->size) {
        int left = i * 2 + 1;
        int right = i * 2 + 2;
        int smallest = left;
        if (right < heap->size && compare_nodes(heap->data[right], heap->data[left]) < 0) {
            smallest = right;
        }
        if (compare_nodes(last_node, heap->data[smallest]) <= 0) break;
        heap->data[i] = heap->data[smallest];
        i = smallest;
    }
    heap->data[i] = last_node;
    return min_node;
}
```

时间复杂度 O(log N)。堆中可能残留"过期条目"（同一状态被更优路径更新后遗留的旧节点引用），由搜索主循环在弹出时依据状态表判定并跳过。

---

## 4. 状态表 —— 已发现状态去重（State Table）

### 4.1 设计定位

原始"CLOSED 表只存已扩展状态"的设计需要额外处理 OPEN 内重复，容易产生重复扩展与指标失真。本设计将**一个哈希表统一覆盖所有已发现状态**，每个状态只保留一条记录，同时记录其最优 `g`、规范节点与是否已关闭：

```c
typedef struct {
    unsigned int key;      // base-9 状态编码
    int          best_g;   // 已知到达该状态的最小 g
    Node*        node;     // 产生 best_g 的规范节点，用于路径回溯与目标输出
    bool         closed;   // 是否已展开
    struct StateEntry* next;  // 冲突链
} StateEntry;

typedef struct {
    StateEntry** buckets;
    int bucket_count;      // 2 的幂
    long count;            // 当前条目数
} StateTable;
```

* 初始 `bucket_count = 1 << 19`（524,288 桶）；8 数码可达状态仅 181,440，负载因子低于 0.35，无需动态扩容。若后续支持更大规模 N 数码，可加入扩容逻辑。
* 表中 `node` 仅为引用，节点由内存池统一释放。

### 4.2 状态编码与哈希函数

#### 1. base-9 状态编码

将 3×3 格局按行优先展开为 9 个数字（0..8），视作 9 位九进制数：

```c
unsigned int state_encode(const int state[3][3]) {
    unsigned int val = 0;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            val = val * 9u + (unsigned int)state[i][j];
    return val;   // 范围 0 .. 9^9 - 1 = 387,420,488
}
```

编码一一对应，且范围小于 $2^{32}$。相比十进制编码，base-9 的取值密度更适合哈希。

#### 2. 混合哈希 + 位掩码

桶数取 2 的幂，用乘法混合后再取低 `log2(bucket_count)` 位：

```c
static inline unsigned int mix_hash(unsigned int x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline StateEntry** bucket_of(StateTable* t, unsigned int key) {
    return &t->buckets[mix_hash(key) & (unsigned int)(t->bucket_count - 1)];
}
```

* **为什么不用直接取模**：`state_encode` 的低位由最后几个格子决定，直接与 2 的幂做掩码会造成明显聚集；乘法混合可打散位分布。
* 相比"选取质数 65537 再取模"，位掩码避免整数除法，代码更短、更快。

### 4.3 核心操作与惰性删除

```c
StateEntry* table_find(StateTable* t, unsigned int key);
bool        table_insert(StateTable* t, unsigned int key, int g, Node* node); // 新增记录
void        table_update(StateEntry* e, int g, Node* node);                   // 刷新为更优 g
```

搜索主循环中的判定流程：

1. **生成子节点** `child`（`g_new`、`h`、`f` 已计算）：
   * `e = table_find(key)`；
   * `e == NULL`：`table_insert(key, g_new, child)` 并入堆；
   * `e != NULL && e->best_g <= g_new`：丢弃 `child`（`generated` 仍计数，不入堆）；
   * `e != NULL && e->best_g > g_new`：`table_update(e, g_new, child)` 并入堆；旧节点引用成为堆中的**过期条目**。
2. **弹出节点** `node`：
   * `e = table_find(key)`；若 `e == NULL || e->closed || node->g != e->best_g` → 该引用已过期或重复，直接跳过（不计入 `expanded`）；
   * 否则 `e->closed = true`，`expanded++`，继续目标判定与邻接展开。

该流程保证：每个状态最多被真正展开一次，且展开时使用的是最小 `g`；堆中残留的过期条目只增加少量内存，不影响正确性，也不影响"生成节点数"这一指标（定义上就包含它们）。

---

## 5. Node 内存池（Pool / Arena）

### 5.1 设计动机

若每个 `Node` 独立 `malloc` 并在搜索结束后逐个 `free`，则需精确追踪"堆中未弹出的节点"与"状态表引用的节点"两类所有权，极易漏释放或重复释放。内存池把所有权集中到一处：

```c
typedef struct PoolChunk {
    struct PoolChunk* next;
    size_t used;              // 本块已使用节点数
    Node   nodes[];           // 柔性数组，块大小 = chunk_nodes
} PoolChunk;

typedef struct {
    PoolChunk* head;
    size_t chunk_nodes;       // 默认 8192
    long total_allocated;     // 累计分配 Node 数
} Pool;
```

### 5.2 接口

```c
Pool* create_pool(size_t chunk_nodes);   // chunk_nodes 为 0 时使用默认值 8192
Node* pool_alloc(Pool* pool);            // 返回已清零 (memset) 的 Node；失败返回 NULL
long  pool_total_allocated(const Pool* pool);
void  free_pool(Pool* pool);             // 遍历 chunk 链表一次性 free
```

### 5.3 特性

* **摊还 O(1)**：每块一次性 `malloc` 8192 个 `Node`，分配单个节点只需指针自增。
* **零碎片**：节点大小固定，块内无碎片。
* **集中释放**：`free_pool` 遍历块链表释放，天然处理"命中目标时堆中仍有大量未弹出节点"的情况。

---

## 6. 内存生命周期管理

1. **分配**：搜索期间所有 `Node` 一律通过 `pool_alloc` 获得；堆数组、状态表桶数组与链表节点各自 `malloc`，由对应模块负责释放。
2. **所有权归属**：
   * `Node` —— 归内存池所有，堆与状态表仅保存指针；
   * 堆数组 —— 归 `MinHeap`；
   * 桶数组与 `StateEntry` —— 归 `StateTable`。
3. **统一释放顺序**：
   1. `free_min_heap`：释放堆数组（不触碰 `Node`）；
   2. `free_state_table`：释放所有 `StateEntry` 与桶数组（不触碰 `Node`）；
   3. `free_pool`：一次性释放全部 `Node`。
4. **失败路径**：任一步骤的 `malloc` / `realloc` 失败，立即释放已建结构并返回错误，上层以退出码 `3` 结束。

该设计下每一项资源都有且仅有一个释放者，既无泄漏也无 double free，与需求文档 3.3 的约定一致。
