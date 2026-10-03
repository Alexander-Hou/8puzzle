#!/usr/bin/env bash
# 8 数码 A* 程序黑盒验收脚本。
# 由 `make test` 调用；对应 docs/01-requirements.md 第 5.2 节的验收项。
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/bin/puzzle"

TARGET="1 2 3 4 5 6 7 8 0"
SOLVABLE="1 2 3 4 0 6 7 5 8"      # 逆序数为偶，可解
UNSOLVABLE="2 8 3 1 6 4 7 0 5"    # 逆序数为奇，不可解
INVALID="1 2 3 4 5 6 7 8 8"       # 重复数字，非法输入
SEED="20261020"
BENCH_N=5
CSV_HEADER="method,start,target,path_len,generated,expanded,peak_open,peak_closed,total_alloc_nodes,time_ms,pruning_rate"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0
ok()  { printf 'PASS  %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf 'FAIL  %s\n' "$1"; fail=$((fail + 1)); }

if [ ! -x "$BIN" ]; then
    printf 'FAIL  %s 不存在：请先完成实现并执行 make\n' "$BIN"
    exit 1
fi

# 从 CSV 中取指定策略的路径长度（第 4 列）
path_len_of() {
    awk -F, -v m="$2" 'NR > 1 && $1 == m { print $4; exit }' "$1"
}

# ---------------------------------------------------------------- 1. 构建产物
ok "bin/puzzle 可执行"

# -------------------------------------------- 2. 固定可解样例：三策略长度一致
# 注意：这里刻意不用关联数组（declare -A 需要 bash 4+），
# 以便在 macOS 自带的 bash 3.2 下也能运行。
LEN_H1=""
LEN_H2=""
LEN_BFS=""
for method in h1 h2 bfs; do
    csv="$WORK/$method.csv"
    "$BIN" -s "$SOLVABLE" -t "$TARGET" -m "$method" --csv "$csv" >/dev/null 2>&1
    value="$(path_len_of "$csv" "$method")"
    case "$method" in
        h1)  LEN_H1="$value" ;;
        h2)  LEN_H2="$value" ;;
        bfs) LEN_BFS="$value" ;;
    esac
done
if [ -n "$LEN_H1" ] && [ "$LEN_H1" = "$LEN_H2" ] && [ "$LEN_H2" = "$LEN_BFS" ]; then
    ok "三策略路径长度一致 (path_len=$LEN_H2)"
else
    bad "三策略路径长度不一致 (h1=${LEN_H1:-?}, h2=${LEN_H2:-?}, bfs=${LEN_BFS:-?})"
fi

# ------------------------------------------------------ 3. 不可解：退出码为 2
"$BIN" -s "$UNSOLVABLE" -t "$TARGET" >/dev/null 2>&1
rc=$?
if [ "$rc" -eq 2 ]; then
    ok "不可解样例退出码为 2"
else
    bad "不可解样例退出码应为 2，实际为 $rc"
fi

# ------------------------------------------------------ 4. 非法输入：退出码为 1
"$BIN" -s "$INVALID" -t "$TARGET" >/dev/null 2>&1
rc=$?
if [ "$rc" -eq 1 ]; then
    ok "非法输入退出码为 1"
else
    bad "非法输入退出码应为 1，实际为 $rc"
fi

# --------------------------------------------- 5. 批量模式：CSV 表头与行数正确
bench_csv="$WORK/bench.csv"
"$BIN" -b "$BENCH_N" --seed "$SEED" --csv "$bench_csv" >/dev/null 2>&1
if [ -f "$bench_csv" ]; then
    header="$(head -n 1 "$bench_csv")"
    rows="$(($(wc -l < "$bench_csv") - 1))"
    if [ "$header" = "$CSV_HEADER" ] && [ "$rows" -eq $((3 * BENCH_N)) ]; then
        ok "批量模式 CSV 表头与 $((3 * BENCH_N)) 行数据正确"
    else
        bad "批量模式 CSV 不符 (rows=$rows, header=$header)"
    fi
else
    bad "批量模式未生成 CSV 文件"
fi

# ------------------------------------------------ 6. --selftest：退出码为 0
"$BIN" --selftest >/dev/null 2>&1
rc=$?
if [ "$rc" -eq 0 ]; then
    ok "--selftest 通过"
else
    bad "--selftest 退出码应为 0，实际为 $rc"
fi

# -------------------------------- 7. -m all -p：只打印一条路径（h2，与单测步数一致）
steps_all="$("$BIN" -s "$SOLVABLE" -t "$TARGET" -m all -p 2>/dev/null | grep -c '^Step ')"
if [ "$steps_all" = "$LEN_H2" ]; then
    ok "-m all -p 只打印 h2 路径 (steps=$steps_all)"
else
    bad "-m all -p 打印步数应等于 h2 路径长度 ${LEN_H2:-?}，实际 $steps_all"
fi

# ------------------- 8. 可复现性：同 seed 的确定性列逐字节一致（耗时列除外）
"$BIN" -b "$BENCH_N" --seed "$SEED" --csv "$WORK/bench2.csv" >/dev/null 2>&1
# 第 10 列为 time_ms，属于测量值，不参与可复现性比较
cut -d, -f1-9,11 "$bench_csv" > "$WORK/bench_a.csv"
cut -d, -f1-9,11 "$WORK/bench2.csv" > "$WORK/bench_b.csv"
if cmp -s "$WORK/bench_a.csv" "$WORK/bench_b.csv"; then
    ok "同 --seed 批量输出的确定性列可复现"
else
    bad "同 --seed 批量输出的确定性列不一致"
fi

# ------------------------------------------------------------------- 汇总
printf '\n%d passed, %d failed\n' "$pass" "$fail"
if [ "$fail" -ne 0 ]; then
    exit 1
fi
