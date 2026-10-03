# 8 数码问题 A* 算法求解程序 —— 软件架构设计文档

## 1. 架构设计目标与原则

为了确保程序的高效性、正确性与良好的代码组织规范，本项目在设计时遵循以下原则：

1. **模块化与低耦合**：将命令行解析、搜索算法核心、展示报表生成以及基础数据结构（堆、状态表、内存池）彻底解耦，各模块遵循单一职责原则（Single Responsibility Principle）。
2. **策略模式（Strategy Pattern）**：通过 **C 语言函数指针** 将启发式评估函数（$h_1, h_2, h_0$）与 A* 搜索算法的主循环解耦，方便灵活切换不同的策略。
3. **展示与逻辑分离（I/O Separation）**：将解路径回溯、棋盘渲染及性能数据打印独立为 `reporter` 模块，搜索核心不参与任何 I/O。
4. **高效且正确的数据结构**：OPEN 表采用基于数组的小顶堆（Min-Heap），状态表采用基于拉链法的哈希表并配合 node 内存池，确保搜索性能、去重能力与内存安全。
5. **可复现性**：所有比较均使用确定性全序，所有指标使用唯一定义，保证同一输入多次运行结果一致。

---

## 2. 总体架构与模块划分

程序采用分层/模块化的结构设计，整个系统的模块依赖关系如下图所示：

```text
main.c
├── cli.h / cli.c            命令行解析：getopt_long、Config、输入校验
├── puzzle.h / puzzle.c      搜索核心：合法性/可解性、h1/h2/h0、astar_solve
│   ├── heap.h / heap.c              OPEN 表（小顶堆 + 确定性比较器）
│   ├── state_table.h / .c           已发现状态表（去重 + best_g + 惰性删除）
│   └── pool.h / pool.c              Node 内存池（批量分配、一次性释放）
└── reporter.h / reporter.c  路径回溯、棋盘渲染、指标/对比表/CSV 输出
```

`astar_solve` 向 `reporter` 交出目标节点与 `SearchMetrics`；`reporter` 不反向依赖搜索逻辑。

依赖方向保持单向：`main` → `cli` / `puzzle` / `reporter`；`puzzle` → `heap` / `state_table` / `pool`。基础模块之间互不依赖。

---

## 3. 模块职责与接口设计

### 3.1 命令行解析模块 (`cli.h` / `cli.c`)

* **职责**：解析终端输入的命令行参数（使用 `getopt_long`），校验输入格式，并将字符串格式的状态转换成 3×3 数组。
* **主要接口**：

```c
typedef enum { METHOD_H1 = 0, METHOD_H2, METHOD_BFS, METHOD_ALL } Method;

typedef enum {
    STATE_OK = 0,
    STATE_ERR_NOT_NUMBER,  // 含非数字记号
    STATE_ERR_COUNT,       // 数字个数不是 9
    STATE_ERR_RANGE,       // 数字超出 0..8
    STATE_ERR_DUPLICATE    // 9 个数字中出现重复
} StateParseStatus;

typedef enum { CLI_RC_OK = 0, CLI_RC_HELP, CLI_RC_ERROR } CliStatus;

typedef struct {
    int  start[3][3];      // -s
    int  target[3][3];     // -t，默认 CLI_DEFAULT_TARGET
    int  has_start;        // 是否提供 -s
    Method method;         // -m，默认 METHOD_H2
    int  print_path;       // -p
    int  repeat;           // -r，默认 1
    int  benchmark;        // -b，>0 表示批量模式
    unsigned int seed;     // --seed，默认 CLI_DEFAULT_SEED
    const char* csv_path;  // --csv，可为 NULL
    int  selftest;         // --selftest
} Config;

CliStatus        parse_cli_args(int argc, char* argv[], Config* config);
StateParseStatus parse_state_string(const char* str, int state[3][3]);
void             print_usage(const char* prog_name);
```

* `Config` 的定义与默认值（默认目标 `CLI_DEFAULT_TARGET`、默认方法 `h2`、默认 `repeat = 1`、默认 `seed = CLI_DEFAULT_SEED`）均由本模块负责。
* 返回值：`CLI_RC_OK` 解析成功；`CLI_RC_HELP` 表示用户请求 `-h/--help`（非错误，`main` 打印用法后以退出码 `0` 结束）；`CLI_RC_ERROR` 表示用法或输入非法，此时原因已写入 `stderr`，`main` 以退出码 `1` 结束。
* 输入校验规则见需求文档 2.2，由 `parse_state_string` 完成：含非数字记号、数字个数不为 9、超出 0..8、出现重复分别返回对应状态码，便于输出精确提示。
* `parse_cli_args` 内部会重置 `getopt` 状态（`optind = 0`、`opterr = 0`），因此允许在同一进程中被反复调用，可直接用于单元测试。

### 3.2 棋盘状态与启发函数模块 (`puzzle.h` / `puzzle.c`)

* **职责**：维护 8 数码棋盘状态、实现空格移动推导、状态编码、输入合法性基础校验、逆序数可解性判定以及不同的评估函数。
* **主要接口**：

```c
bool is_valid_state(const int state[3][3]);                 // 恰为 0..8 的一个排列
int inversion_count(const int state[3][3]);                 // 忽略空格的逆序数
bool is_solvable(const int start[3][3], const int target[3][3]);
unsigned int state_encode(const int state[3][3]);           // base-9 编码，见数据结构文档
int h_misplaced_tiles(const int state[3][3], const int target[3][3]);  // h1
int h_manhattan_distance(const int state[3][3], const int target[3][3]); // h2
int h_zero(const int state[3][3], const int target[3][3]);  // h0 = 0

typedef int (*HeuristicFunc)(const int state[3][3], const int target[3][3]);
```

### 3.3 A* 算法核心引擎与策略模式 (`puzzle.c`)

* **职责**：控制搜索的主循环。利用函数指针接收不同的启发函数，通过 OPEN 表（小顶堆）、状态表与 Node 内存池协作完成搜索，不参与任何 I/O。
* **核心接口**：

```c
typedef struct {
    int    path_len;          // 目标节点 g 值
    long   generated;         // 扩展产生的候选子节点数（含被 best_g 比较丢弃的，不要求实际分配）
    long   expanded;          // 真正展开（关闭）的节点数
    long   peak_open;         // 堆内元素数量峰值（含过期条目）
    long   peak_closed;       // 关闭状态数峰值
    long   total_alloc_nodes; // 本次搜索实际分配 Node 数（<= generated）
    double time_ms;           // 仅搜索段耗时
} SearchMetrics;

Node* astar_solve(const int start[3][3], const int target[3][3],
                  HeuristicFunc h_func, SearchMetrics* metrics, Pool* pool);
```

* **返回**：目标节点指针（其 `parent` 链可回溯出完整路径），失败返回 `NULL`；节点内存由 `pool` 统一持有，调用方不得 `free`。
* **目标判定时机**：仅在节点被**弹出**时判断是否为目标，这是保证最优性的关键。
* **重复状态处理**：状态表统一记录每个已发现状态的 `best_g`、规范节点指针与 `closed` 标志，覆盖 OPEN 与 CLOSED：
  * 生成子节点时计算 `g_new`：
    * 状态表中无记录 → 登记并入堆；
    * 已有记录且 `best_g <= g_new` → 丢弃该子节点（仍计入 `generated`）；
    * 已有记录且 `best_g > g_new` → 更新记录的 `best_g` 与规范节点，并入堆（旧堆条目成为过期条目）。
  * 弹出节点时先判过期：`closed == true` 或 `node->g != best_g` → 丢弃该引用（不计入 `expanded`），继续下一轮。
* **最优性依据**：$h_1$、$h_2$ 可采纳且一致，因此关闭节点无需重新打开；`best_g` 比较保证每个状态最终以最小代价被展开。**不得**采用"只要状态已出现过就丢弃"的简化，否则在一致启发式下也可能错过最优路径。
* **界限保护**：`generated` 超过节点数上限时返回 `NULL` 并由上层以退出码 `3` 报告。

### 3.4 OPEN 表 —— 小顶堆模块 (`heap.h` / `heap.c`)

* **职责**：高效维护待扩展节点的优先队列，每次在 O(log N) 时间内弹出优先级最高的节点指针。
* **主要接口**：

```c
MinHeap* create_min_heap(int initial_capacity);
void     heap_push(MinHeap* heap, Node* node);   // 扩容失败时返回错误信号
Node*    heap_pop(MinHeap* heap);
int      heap_size(const MinHeap* heap);
void     free_min_heap(MinHeap* heap);           // 仅释放堆数组，不释放 Node
```

* **确定性比较器**（全序，保证可复现）：

```c
int compare_nodes(const Node* a, const Node* b);  // <0 表示 a 优先
```

比较顺序为：`f` 升序 → `g` 降序 → `h` 升序 → 状态编码升序（使用节点缓存的 `Node.code`，因此 heap 模块无需依赖 puzzle 模块）。前两项是常规 tie-break（优先更接近目标的深层节点）；最后一项消除残余并列，使运行结果完全确定。注意 $h \equiv 0$ 时 $f \equiv g$，此时该规则自然退化为"层序 + 编码序"。

### 3.5 状态表 —— 已发现状态去重模块 (`state_table.h` / `state_table.c`)

* **职责**：记录所有**已发现**状态（涵盖 OPEN 与 CLOSED），实现平均 O(1) 的查找/更新，并支持惰性删除所需的 `best_g` 判定。
* **主要接口**：

```c
typedef struct {
    unsigned int key;      // base-9 状态编码
    int          best_g;   // 已知最优 g
    Node*        node;     // 对应的规范节点（路径回溯使用）
    bool         closed;   // 是否已展开
    struct StateEntry* next;  // 冲突链
} StateEntry;

StateTable*  create_state_table(void);
StateEntry*  table_find(StateTable* table, unsigned int key);
bool         table_insert(StateTable* table, unsigned int key, int g, Node* node);
void         table_update(StateEntry* entry, int g, Node* node);  // 刷新为更优 g
long         table_count(const StateTable* table);                // 条目数（用于指标）
void         free_state_table(StateTable* table);                // 仅释放表结构
```

* **哈希方案**：桶数取 2 的幂（默认 $2^{19}$），索引 = 混合哈希(编码) & (桶数 − 1)；哈希值用乘法混合（乘奇数常量后取高位），避免 base-9 编码低位规律导致的聚集。
* **不存储节点所有权**：表中 `node` 仅为引用，节点由 `pool` 统一释放。

### 3.6 Node 内存池模块 (`pool.h` / `pool.c`)

* **职责**：批量分配与统一释放 `Node`，消除逐节点 `free` 带来的泄漏与 double free 风险，并统计累计分配节点数。
* **主要接口**：

```c
Pool* create_pool(size_t chunk_nodes);   // 默认每块 8192 个 Node
Node* pool_alloc(Pool* pool);            // 返回已清零的 Node，失败返回 NULL
long  pool_total_allocated(const Pool* pool);
void  free_pool(Pool* pool);             // 一次性释放所有块
```

### 3.7 路径展示与报表模块 (`reporter.h` / `reporter.c`)

* **职责**：独立负责解路径回溯、棋盘渲染可视化、单次运行性能指标打印、多策略汇总对比表格渲染、批量模式 CSV 输出。`-m all` 与 `-p` 同时出现时只渲染 $h_2$ 的完整路径。
* **主要接口**：

```c
void print_board(const int state[3][3]);
void print_solution_path(const Node* goal_node);
/* min/max 为 NULL 表示只运行一次；repeat > 1 时打印中位数与 [最小值, 最大值] */
void print_metrics_report(const SearchMetrics* median, const SearchMetrics* min,
                          const SearchMetrics* max, const char* method_name);
void print_comparison_table(const SearchMetrics metrics[], const char* names[], int count);
void write_csv_header(FILE* fp);
/* pruning_rate 为百分比数值；传 NAN 表示该列留空（单策略模式） */
void write_csv_row(FILE* fp, const int start[3][3], const int target[3][3],
                   const char* method_name, const SearchMetrics* metrics, double pruning_rate);
```

每个移动行的固定前缀为 `Step `（如 `Step 1: Move 5 UP`），便于 `tests/run_tests.sh` 通过 `grep -c '^Step '` 统计实际打印的步数。

---

## 4. 关键算法流程

```text
[开始]
  |
  v
解析命令行参数 ---(用法错误)---> 报错 + 用法提示 ---> [退出码 1]
  |
  v
输入合法性校验（恰好 9 个数，且为 0..8 各一次）
  |
  v
逆序数奇偶性判定 ---(奇偶不同)---> 提示无解 ---> [退出码 2]
  |
  v  (奇偶相同：可解)
创建 内存池 / 状态表 / 小顶堆；创建初始节点，登记状态表并压入堆
  |
  v
<< 循环开始 >>
  |
  OPEN 表为空？---(是)---> 搜索失败/无解 ---> [退出码 2]
  |
 (否)
  |
  v
弹出 f 最小的堆顶节点
  |
  已关闭 或 node->g != best_g？---(是)---> 丢弃该引用，回到 << 循环开始 >>
  |
 (否)
  |
  v
是目标状态？---(是)---> 目标节点不标记 closed、不计入 expanded ---> 交由 reporter 输出 ---> [结束]
  |
 (否)
  |
  v
标记 closed，expanded++；生成上下左右可达子节点（过滤越界），计算 g/h/f
  |
  v
对每个子节点与状态表 best_g 比较：
  - 无记录            ---> 登记 + 入堆
  - best_g <= g_new   ---> 丢弃（generated 仍计数）
  - best_g >  g_new   ---> 更新记录 + 入堆（旧堆条目转为过期）
  |
  +---> 回到 << 循环开始 >>

结束（正常或异常路径）：先释放堆与状态表结构，最后 pool_free_all 一次性释放全部 Node。
```

---

## 5. 项目目录结构规划

```text
8puzzle/
├── README.md                   # 项目介绍、构建与用法说明
├── Makefile                    # 构建脚本（all / test / clean）
├── .gitignore                  # Git 忽略规则
├── docs/
│   ├── 01-requirements.md      # 需求分析文档
│   ├── 02-architecture.md      # 软件架构设计文档
│   ├── 03-data-structure.md    # 核心数据结构设计
│   └── 04-experiment-report.md # 实验报告骨架（数据与截图待实现后回填）
├── include/
│   ├── cli.h
│   ├── node.h                  # 共享的 Node 定义（堆 / 状态表 / 内存池 / 搜索共用）
│   ├── puzzle.h
│   ├── heap.h
│   ├── state_table.h
│   ├── pool.h
│   └── reporter.h
├── src/
│   ├── main.c
│   ├── cli.c
│   ├── puzzle.c
│   ├── heap.c
│   ├── state_table.c
│   ├── pool.c
│   └── reporter.c
└── tests/
    └── run_tests.sh            # 黑盒验收脚本
```

---

## 6. 构建目标与退出码

### 6.1 Makefile 目标

| 目标 | 行为 |
| --- | --- |
| `make` / `make all` | 以 `-std=c11 -Wall -Wextra -O2` 编译 `src/*.c`，输出可执行文件 `bin/puzzle`。 |
| `make test-cli` | 编译并运行 cli 模块单元测试（`tests/test_cli.c`），不依赖整程序构建。 |
| `make test-puzzle` | 编译并运行 puzzle 模块单元测试（`tests/test_puzzle.c`），不依赖整程序构建。 |
| `make test` | 构建可执行文件，运行全部模块单元测试，再执行 `tests/run_tests.sh` 黑盒验收。 |
| `make clean` | 删除 `bin/` 与所有 `*.o` 中间产物。 |

### 6.2 退出码

与需求文档 2.6 保持一致：

| 退出码 | 含义 |
| --- | --- |
| `0` | 求解成功 |
| `1` | 参数或输入非法 |
| `2` | 判定为无解 |
| `3` | 内存分配失败或达到节点数上限 |
| `4` | `--selftest` 断言失败 |
