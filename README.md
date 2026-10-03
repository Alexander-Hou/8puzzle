# 8 数码问题 A* 求解器

用 C 语言实现的 8 数码（8-Puzzle）求解程序：求出从任意合法初始状态到目标状态的**最优解路径**，
并用同一套指标对比三种搜索策略，配套设计文档、实验报告、自动化测试与绘图脚本。

## 功能特性

* **求解**：A* 算法求最优解，可打印从初始状态到目标状态的完整移动步骤与棋盘矩阵；
* **策略对比**：一次性运行三种策略并输出对比表；
* **可解性预判**：求解前用逆序数判定有无解，避免无效搜索；
* **性能指标**：路径长度、生成/扩展节点数、OPEN/CLOSED 峰值、耗时、相对 BFS 剪枝率；
* **批量基准**：随机生成 N 个可解初态批量评测，结果可导出 CSV，同一随机种子结果可复现；
* **可验证**：内建 `--selftest` 自检，并有单元测试与黑盒验收脚本。

| 策略 | 启发函数 | 说明 |
| --- | --- | --- |
| `h1` | 放错位置的数码个数 | 信息较粗 |
| `h2` | 曼哈顿距离之和（默认） | 信息更细，通常扩展节点最少 |
| `bfs` | `h ≡ 0`（无启发） | 盲目搜索，作为对比基准 |

## 环境准备

程序是 C11 代码，构建需要 **GNU make**，并用到两个 **POSIX 接口**（`getopt_long` 与 `clock_gettime`），
因此需要类 Unix 环境；不依赖任何第三方 C 库。绘图脚本只需要 Python 3 标准库，各平台通用。

| 平台 | 一次性准备 | 之后 |
| --- | --- | --- |
| **Linux** | Debian/Ubuntu：`sudo apt install build-essential python3`<br>Arch：`sudo pacman -S base-devel python` | 按下方"快速开始"执行 |
| **macOS** | `xcode-select --install`（提供 clang、make 与 python3） | 同上 |
| **Windows（使用WSL2）** | 管理员 PowerShell 执行 `wsl --install -d Ubuntu`，重启后进入 Ubuntu，再执行 `sudo apt update && sudo apt install -y build-essential python3` | 与 Linux 完全相同 |

### Windows 用户注意事项

* **使用 WSL**：它就是完整的 Linux，本项目的 Makefile、测试脚本与绘图脚本都能原样使用。
  项目建议放在 Linux 家目录（如 `/home/<用户名>/8puzzle`），不要放在 `/mnt/c/...`，以免权限与文件系统性能问题。
* **只想看结果**：已经编译好的 `bin/puzzle` 是 Linux 可执行文件，Windows 上可以直接在 WSL 运行。

### macOS 用户注意事项

* 系统自带的 `/bin/bash` 是 3.2，本项目的测试脚本已刻意避免 bash 4+ 语法，无需额外安装 bash。
* 如提示缺少 `make`，说明还没装 Xcode 命令行工具，执行 `xcode-select --install` 即可。
* macOS 提供同样的 POSIX 接口（clang 自带 `getopt_long`，`clock_gettime` 自 10.12 起可用），
  预期可直接构建，但未在 macOS 机器上实测。

## 快速开始

```sh
make                                              # 构建，产物为 bin/puzzle
bin/puzzle --help                                 # 查看全部参数
bin/puzzle -s "1 2 3 4 0 6 7 5 8" -m all          # 三策略对比
```

## 用法

### 命令行参数

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `-s, --start <string>` | 必填（`--selftest` 与 `-b` 时除外） | 初始状态，9 个以空格分隔的数字 |
| `-t, --target <string>` | `1 2 3 4 5 6 7 8 0` | 目标状态 |
| `-m, --method <h1\|h2\|bfs\|all>` | `h2` | 求解策略；`all` 依次运行三种并输出对比表 |
| `-p, --print-path` | 关闭 | 打印完整移动步骤与棋盘矩阵 |
| `-r, --repeat <N>` | `1` | 重复搜索 N 次并输出中位数与 `[最小值, 最大值]` |
| `-b, --benchmark <N>` | 关闭 | 随机生成 N 个可解初态，批量运行三种策略 |
| `--seed <uint32>` | 固定常量 | 批量模式的随机种子，保证结果可复现 |
| `--csv <file>` | 无 | 将指标写入 CSV 文件 |
| `--selftest` | 关闭 | 运行内建自检，不执行常规求解 |
| `-h, --help` | — | 打印帮助与示例 |

### 输出指标

| 指标 | 含义 |
| --- | --- |
| 解路径长度 | 最优解的步数 |
| 生成节点数 | 搜索过程中产生的候选状态数 |
| 扩展节点数 | 真正展开并生成后继的状态数 |
| OPEN 峰值 / CLOSED 峰值 | 搜索过程中的待扩展 / 已展开状态数峰值，反映内存占用 |
| 累计分配节点数 | 实际分配的状态节点总数 |
| 搜索耗时 | 仅统计搜索过程，单位为毫秒 |
| 剪枝率 | 相对 BFS 基准减少的节点比例（批量与 `all` 模式下给出） |

### 常用示例

```sh
# 打印某个初始状态的完整解路径
bin/puzzle -s "1 2 3 4 0 6 7 5 8" -m h2 -p

# 指定目标状态
bin/puzzle -s "1 2 3 4 0 6 7 5 8" -t "8 7 6 5 4 3 2 1 0" -m all

# 重复 5 次取中位数，抑制计时抖动
bin/puzzle -s "0 1 2 3 4 5 6 7 8" -m all -r 5

# 批量基准并导出 CSV
bin/puzzle -b 50 --seed 20261020 --csv results.csv

# 内建自检
bin/puzzle --selftest
```

### 不可解示例

```sh
bin/puzzle -s "2 8 3 1 6 4 7 0 5"
# 该状态与目标状态的逆序数奇偶性不同：程序提示无解，退出码为 2
```

### 退出码

| 退出码 | 含义 |
| --- | --- |
| `0` | 求解成功 |
| `1` | 参数或输入非法 |
| `2` | 判定为无解 |
| `3` | 内存分配失败或达到节点数上限 |
| `4` | `--selftest` 自检失败 |

## 图表生成

批量模式导出的 CSV 可以直接画成报告用的图，脚本**只依赖 Python 标准库**，无需安装任何第三方包。

```sh
# 1) 生成数据（在项目根目录）
bin/puzzle -b 50 --seed 20261020 --csv results.csv

# 2) 画图（任意目录下都可执行）
python3 scripts/show_graph.py
```

输出 `scripts/out/analysis.svg` 与 `scripts/out/analysis.png`，包含四张子图：

1. 搜索耗时 vs 最优路径长度（对数纵轴）；
2. 扩展节点数 vs 最优路径长度（对数纵轴）；
3. 平均节点开销对比（生成 / 扩展 / 累计分配，对数纵轴）；
4. 剪枝率分布（相对 BFS，仅比较 h1 与 h2）。

可选参数：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--csv` | `results.csv`（项目根目录） | 指标 CSV 路径 |
| `--out` | `scripts/out/analysis.png` | 输出路径，扩展名决定是否额外栅格化 |
| `--width` | `1400` | PNG 像素宽度 |

说明：

* 无论 `--out` 是什么扩展名，都会写出同名 `.svg` 矢量图（插入 Word 最清晰）；
* 若 `--out` 为 `.png` 且系统存在 `rsvg-convert`、`inkscape` 或 ImageMagick，则额外生成 PNG；否则只输出 SVG 并给出提示。
  需要 PNG 时可按平台安装：Linux `librsvg2-bin`（Debian/Ubuntu）或 `librsvg`（Arch）、macOS `brew install librsvg`、
  Windows 安装 Inkscape，或直接用浏览器打开 SVG 后截图；
* 中文标签会自动使用系统已有的 CJK 字体，图内无乱码。

## 项目结构

```text
8puzzle/
├── docs/         设计文档与实验报告（报告内含实测数据与截图）
├── include/      C 头文件
├── src/          C 源代码
├── tests/        单元测试与黑盒验收脚本
├── scripts/      图表生成脚本（Python，零第三方依赖）
├── Makefile      构建 / 测试 / 清理
└── README.md
```

实现细节（模块划分、数据结构、算法流程、指标口径）见 `docs/` 下的设计文档。

## 构建与测试

```sh
make                  # 构建，产物为 bin/puzzle
make test             # 运行全部单元测试与黑盒验收
make test-astar       # 只跑单个模块：test-cli / test-puzzle / test-pool /
                      #               test-heap / test-state-table / test-astar
make clean            # 清理 build/ 与 bin/
```

编译选项为 `-std=c11 -Wall -Wextra -O2`。测试共分两层：面向模块的单元测试（C 语言编写，直接断言接口行为）
与 `tests/run_tests.sh` 黑盒验收（校验命令行、退出码、CSV 格式与输出可复现性）。

## 文档

| 文件 | 内容 |
| --- | --- |
| [`docs/01-requirements.md`](docs/01-requirements.md) | 需求分析、命令行契约、指标定义、测试与验收标准 |
| [`docs/02-architecture.md`](docs/02-architecture.md) | 模块划分、接口设计与求解流程 |
| [`docs/03-data-structure.md`](docs/03-data-structure.md) | 核心数据结构与内存管理设计 |
| [`docs/04-experiment-report.md`](docs/04-experiment-report.md) | 实验报告（含实测数据、图表与截图） |
