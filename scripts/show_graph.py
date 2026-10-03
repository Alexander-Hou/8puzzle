#!/usr/bin/env python3
"""8 数码 A* 实验：从指标 CSV 生成四联分析图（纯标准库，无需安装任何包）。

用法（在项目任意位置都可执行）:
    python3 scripts/show_graph.py
    python3 scripts/show_graph.py --csv results.csv --out out/analysis.png

输出：
    * 始终写出同名 SVG（矢量，可直接插入 Word / 浏览器查看）；
    * 若 --out 指定 .png 且系统存在 rsvg-convert / inkscape / 浏览器等转换器，
      再额外栅格化出 PNG。
"""

from __future__ import annotations

import argparse
import csv
import math
import shutil
import subprocess
import sys
from pathlib import Path
from xml.sax.saxutils import escape

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent

# 画布与版式（与原先 matplotlib 的 figsize=(14,10) 对应）
CANVAS_W, CANVAS_H = 1400, 1000
MARGIN_LEFT, MARGIN_RIGHT, MARGIN_TOP, MARGIN_BOTTOM = 95, 45, 72, 60
GAP_X, GAP_Y = 85, 78

FONT = ("'Noto Sans CJK SC','Source Han Sans CN','Noto Sans CJK JP',"
        "'WenQuanYi Zen Hei','SimHei',sans-serif")

METHOD_ORDER = ["h1", "h2", "bfs"]
METHOD_LABEL = {"h1": "h1 放错数码数", "h2": "h2 曼哈顿距离", "bfs": "bfs (h=0)"}
METHOD_COLOR = {"h1": "#4c72b0", "h2": "#dd8452", "bfs": "#55a868"}
METHOD_MARKER = {"h1": "circle", "h2": "cross", "bfs": "square"}

GRID = "#e2e2e2"
FRAME = "#cccccc"
INK = "#222222"
INK_SOFT = "#666666"

REQUIRED_COLUMNS = ("method", "path_len", "time_ms", "generated", "expanded",
                    "total_alloc_nodes")


# --------------------------------------------------------------------- 数据

def load_rows(path: Path) -> list[dict]:
    """读取指标 CSV，返回规范化后的行列表。"""
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        fields = set(reader.fieldnames or [])
        missing = [name for name in REQUIRED_COLUMNS if name not in fields]
        if missing:
            raise ValueError(f"CSV 缺少必需列：{', '.join(missing)}")

        rows = []
        for raw in reader:
            method = (raw.get("method") or "").strip()
            if not method:
                continue
            pruning = (raw.get("pruning_rate") or "").strip()
            rows.append({
                "method": method,
                "path_len": int(raw["path_len"]),
                "generated": int(raw["generated"]),
                "expanded": int(raw["expanded"]),
                "total_alloc_nodes": int(raw["total_alloc_nodes"]),
                "time_ms": float(raw["time_ms"]),
                "pruning_rate": float(pruning) if pruning else None,
            })
    if not rows:
        raise ValueError("CSV 中没有数据行")
    return rows


def quantile(sorted_values: list[float], q: float) -> float:
    """线性插值分位数（与 numpy.percentile 默认行为一致）。"""
    n = len(sorted_values)
    if n == 1:
        return float(sorted_values[0])
    pos = (n - 1) * q
    low = math.floor(pos)
    high = math.ceil(pos)
    if low == high:
        return float(sorted_values[int(pos)])
    return sorted_values[low] + (sorted_values[high] - sorted_values[low]) * (pos - low)


# ------------------------------------------------------------------ SVG 基元

class Svg:
    """极简 SVG 画布，只提供绘图需要的几种图元。"""

    def __init__(self, width: int, height: int) -> None:
        self.width = width
        self.height = height
        self.parts: list[str] = [f'<rect width="{width}" height="{height}" fill="#ffffff"/>']

    def text(self, x: float, y: float, content: str, size: float = 12, anchor: str = "start",
             fill: str = INK, weight: str = "normal", rotate: float | None = None) -> None:
        transform = f' transform="rotate({rotate} {x:.1f} {y:.1f})"' if rotate is not None else ""
        self.parts.append(
            f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" text-anchor="{anchor}" '
            f'fill="{fill}" font-weight="{weight}"{transform}>{escape(content)}</text>'
        )

    def line(self, x1: float, y1: float, x2: float, y2: float, stroke: str = INK,
             width: float = 1.0, dash: str | None = None, opacity: float | None = None) -> None:
        extra = f' stroke-dasharray="{dash}"' if dash else ""
        if opacity is not None:
            extra += f' stroke-opacity="{opacity}"'
        self.parts.append(
            f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" '
            f'stroke="{stroke}" stroke-width="{width}"{extra}/>'
        )

    def rect(self, x: float, y: float, w: float, h: float, fill: str = "none",
             stroke: str = "none", width: float = 1.0, opacity: float | None = None) -> None:
        extra = f' fill-opacity="{opacity}"' if opacity is not None else ""
        self.parts.append(
            f'<rect x="{x:.1f}" y="{y:.1f}" width="{max(w, 0):.1f}" height="{max(h, 0):.1f}" '
            f'fill="{fill}" stroke="{stroke}" stroke-width="{width}"{extra}/>'
        )

    def circle(self, cx: float, cy: float, r: float, fill: str, opacity: float | None = None,
               stroke: str = "none") -> None:
        extra = f' fill-opacity="{opacity}"' if opacity is not None else ""
        self.parts.append(
            f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" fill="{fill}" '
            f'stroke="{stroke}"{extra}/>'
        )

    def marker(self, shape: str, cx: float, cy: float, size: float, color: str,
               opacity: float | None = None) -> None:
        if shape == "circle":
            self.circle(cx, cy, size, color, opacity=opacity)
        elif shape == "square":
            self.rect(cx - size, cy - size, size * 2, size * 2, fill=color, opacity=opacity)
        else:  # cross
            self.line(cx - size, cy - size, cx + size, cy + size, stroke=color, width=1.6)
            self.line(cx - size, cy + size, cx + size, cy - size, stroke=color, width=1.6)

    def render(self) -> str:
        head = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.width}" '
                f'height="{self.height}" viewBox="0 0 {self.width} {self.height}" '
                f'font-family="{FONT}">')
        return "\n".join([head, *self.parts, "</svg>"])


# ---------------------------------------------------------------- 坐标与刻度

class LogAxis:
    """对数纵轴：把数据值映射到像素。"""

    def __init__(self, values: list[float], px_top: float, px_bottom: float) -> None:
        positives = [v for v in values if v > 0] or [1.0]
        self.low = math.floor(math.log10(min(positives)))
        self.high = math.ceil(math.log10(max(positives)))
        if self.high <= self.low:
            self.high = self.low + 1
        self.px_top = px_top
        self.px_bottom = px_bottom

    def to_px(self, value: float) -> float:
        value = max(value, 10.0 ** self.low)
        ratio = (math.log10(value) - self.low) / (self.high - self.low)
        return self.px_bottom - ratio * (self.px_bottom - self.px_top)

    def ticks(self) -> list[float]:
        return [10.0 ** k for k in range(self.low, self.high + 1)]


class LinearAxis:
    def __init__(self, vmin: float, vmax: float, px_low: float, px_high: float) -> None:
        if vmax <= vmin:
            vmax = vmin + 1.0
        self.vmin, self.vmax = vmin, vmax
        self.px_low, self.px_high = px_low, px_high

    def to_px(self, value: float) -> float:
        ratio = (value - self.vmin) / (self.vmax - self.vmin)
        return self.px_low + ratio * (self.px_high - self.px_low)


def format_number(value: float, log_axis: bool = False) -> str:
    if log_axis:
        if value >= 1:
            return f"{value:,.0f}"
        return f"{value:g}"
    if abs(value - round(value)) < 1e-9:
        return f"{int(round(value)):,}"
    return f"{value:,.2f}"


def format_count(value: float) -> str:
    """节点数等计数量按整数显示，避免 "27,101.40" 这种噪声。"""
    if value >= 100:
        return f"{value:,.0f}"
    return f"{value:,.2f}"


# ------------------------------------------------------------------ 各子图

def panel_box(index: int) -> tuple[float, float, float, float]:
    """返回第 index 个（0..3）子图的绘图区 (left, top, right, bottom)。"""
    panel_w = (CANVAS_W - MARGIN_LEFT - MARGIN_RIGHT - GAP_X) / 2
    panel_h = (CANVAS_H - MARGIN_TOP - MARGIN_BOTTOM - GAP_Y) / 2
    col, row = index % 2, index // 2
    left = MARGIN_LEFT + col * (panel_w + GAP_X)
    top = MARGIN_TOP + row * (panel_h + GAP_Y)
    return left, top, left + panel_w, top + panel_h


def panel_background(svg: Svg, box: tuple[float, float, float, float]) -> None:
    """先画底板：必须在所有数据图元之前调用，否则会把数据盖住。"""
    left, top, right, bottom = box
    svg.rect(left, top, right - left, bottom - top, fill="#ffffff", stroke=FRAME)


def panel_titles(svg: Svg, box: tuple[float, float, float, float], title: str,
                 xlabel: str, ylabel: str) -> None:
    """最后画标题，保证文字压在图元之上。"""
    left, top, right, bottom = box
    svg.text((left + right) / 2, top - 12, title, size=14, anchor="middle", weight="bold")
    if xlabel:
        svg.text((left + right) / 2, bottom + 42, xlabel, size=12, anchor="middle")
    svg.text(left - 62, (top + bottom) / 2, ylabel, size=12, anchor="middle", rotate=-90)


def draw_legend(svg: Svg, x: float, y: float, methods: list[str], spacing: float = 20) -> None:
    for i, method in enumerate(methods):
        cy = y + i * spacing
        svg.marker(METHOD_MARKER.get(method, "circle"), x, cy, 4.5,
                   METHOD_COLOR.get(method, INK))
        svg.text(x + 12, cy + 4, METHOD_LABEL.get(method, method), size=11)


def draw_scatter(svg: Svg, box: tuple[float, float, float, float], rows: list[dict],
                 y_field: str, title: str, ylabel: str, methods: list[str]) -> None:
    left, top, right, bottom = box
    panel_background(svg, box)
    inner_left, inner_right = left + 8, right - 12
    inner_top, inner_bottom = top + 16, bottom - 34

    x_values = [row["path_len"] for row in rows]
    scale_x = LinearAxis(min(x_values) - 0.5, max(x_values) + 0.5, inner_left, inner_right)

    # 对数纵轴不能画 0，统一夹到 1e-3
    y_values = [max(float(row[y_field]), 1e-3) for row in rows]
    scale_y = LogAxis(y_values, inner_top, inner_bottom)

    for tick in scale_y.ticks():
        py = scale_y.to_px(tick)
        svg.line(inner_left, py, inner_right, py, stroke=GRID)
        svg.text(inner_left - 8, py + 4, format_number(tick, log_axis=True), size=11,
                 anchor="end", fill=INK_SOFT)

    # X 轴刻度：整数步数，最多 8 个
    x_min, x_max = min(x_values), max(x_values)
    step = max(1, math.ceil((x_max - x_min) / 7)) if x_max > x_min else 1
    tick_x = x_min
    while tick_x <= x_max + 1e-9:
        px = scale_x.to_px(tick_x)
        svg.line(px, inner_top, px, inner_bottom, stroke="#f2f2f2")
        svg.text(px, inner_bottom + 18, str(int(tick_x)), size=11, anchor="middle",
                 fill=INK_SOFT)
        tick_x += step

    for method in methods:
        for row in rows:
            if row["method"] != method:
                continue
            svg.marker(METHOD_MARKER.get(method, "circle"),
                       scale_x.to_px(row["path_len"]),
                       scale_y.to_px(max(float(row[y_field]), 1e-3)),
                       4.2, METHOD_COLOR.get(method, INK), opacity=0.85)

    svg.line(inner_left, inner_bottom, inner_right, inner_bottom, stroke=INK, width=1.2)
    svg.line(inner_left, inner_top, inner_left, inner_bottom, stroke=INK, width=1.2)
    draw_legend(svg, left + 26, top + 34, methods)
    panel_titles(svg, box, title, "最优路径长度（步）", ylabel)


def draw_bars(svg: Svg, box: tuple[float, float, float, float], rows: list[dict],
              methods: list[str]) -> None:
    left, top, right, bottom = box
    panel_background(svg, box)
    inner_left, inner_right = left + 72, right - 12
    inner_top, inner_bottom = top + 16, bottom - 34

    metrics = [("generated", "生成节点"), ("expanded", "扩展节点"),
               ("total_alloc_nodes", "累计分配")]
    averages: dict[str, dict[str, float]] = {}
    for method in methods:
        subset = [row for row in rows if row["method"] == method]
        if not subset:
            continue
        averages[method] = {
            key: sum(float(row[key]) for row in subset) / len(subset) for key, _ in metrics
        }

    all_values = [value for per_method in averages.values() for value in per_method.values()]
    scale_y = LogAxis(all_values or [1.0], inner_top, inner_bottom)

    for tick in scale_y.ticks():
        py = scale_y.to_px(tick)
        svg.line(inner_left, py, inner_right, py, stroke=GRID)
        svg.text(inner_left - 8, py + 4, format_number(tick, log_axis=True), size=11,
                 anchor="end", fill=INK_SOFT)

    group_width = (inner_right - inner_left) / len(metrics)
    bar_width = min(38.0, group_width / (len(methods) + 1))
    baseline = inner_bottom

    for gi, (key, label) in enumerate(metrics):
        center = inner_left + group_width * (gi + 0.5)
        total_width = bar_width * len(methods)
        start = center - total_width / 2
        for mi, method in enumerate(methods):
            if method not in averages:
                continue
            value = averages[method][key]
            bx = start + mi * bar_width
            by = scale_y.to_px(value)
            svg.rect(bx + 2, by, bar_width - 4, baseline - by,
                     fill=METHOD_COLOR.get(method, INK), opacity=0.92)
            svg.text(bx + bar_width / 2, by - 6, format_count(value), size=10,
                     anchor="middle", fill=INK, rotate=-90)
        svg.text(center, inner_bottom + 18, label, size=12, anchor="middle")

    svg.line(inner_left, inner_bottom, inner_right, inner_bottom, stroke=INK, width=1.2)
    svg.line(inner_left, inner_top, inner_left, inner_bottom, stroke=INK, width=1.2)
    # 图例放在右上角，用色块表示
    legend_x = right - 150
    for i, method in enumerate(methods):
        cy = top + 30 + i * 20
        svg.rect(legend_x, cy - 8, 11, 11, fill=METHOD_COLOR.get(method, INK))
        svg.text(legend_x + 16, cy + 2, METHOD_LABEL.get(method, method), size=11)
    panel_titles(svg, box, "平均节点开销（对数纵轴）", "", "节点数（个）")


def draw_boxplot(svg: Svg, box: tuple[float, float, float, float], rows: list[dict],
                 methods: list[str]) -> None:
    left, top, right, bottom = box
    panel_background(svg, box)
    inner_left, inner_right = left + 72, right - 12
    inner_top, inner_bottom = top + 16, bottom - 34

    groups = [(method, sorted(row["pruning_rate"] for row in rows
                              if row["method"] == method and row["pruning_rate"] is not None))
              for method in methods]
    groups = [(method, values) for method, values in groups if values]

    if not groups:
        svg.text((left + right) / 2, (top + bottom) / 2,
                 "CSV 中没有可用的剪枝率（单策略模式该列为空）",
                 size=12, anchor="middle", fill=INK_SOFT)
        panel_titles(svg, box, "剪枝率分布（相对 BFS）", "启发式策略", "剪枝率（%）")
        return

    low = math.floor(min(min(values) for _, values in groups) / 5) * 5
    scale_y = LinearAxis(low, 100.0, inner_bottom, inner_top)

    tick = low
    while tick <= 100.0 + 1e-9:
        py = scale_y.to_px(tick)
        svg.line(inner_left, py, inner_right, py, stroke=GRID)
        svg.text(inner_left - 8, py + 4, format_number(tick), size=11, anchor="end",
                 fill=INK_SOFT)
        tick += 5.0

    slot = (inner_right - inner_left) / len(groups)
    for gi, (method, values) in enumerate(groups):
        center = inner_left + slot * (gi + 0.5)
        color = METHOD_COLOR.get(method, INK)
        q1 = quantile(values, 0.25)
        median = quantile(values, 0.50)
        q3 = quantile(values, 0.75)
        iqr = q3 - q1
        lower_limit, upper_limit = q1 - 1.5 * iqr, q3 + 1.5 * iqr
        inside = [v for v in values if lower_limit <= v <= upper_limit]
        whisker_low, whisker_high = min(inside), max(inside)

        box_w = min(90.0, slot * 0.45)
        bx0, bx1 = center - box_w / 2, center + box_w / 2
        svg.rect(bx0, scale_y.to_px(q3), box_w, scale_y.to_px(q1) - scale_y.to_px(q3),
                 fill=color, stroke=INK, width=1.0, opacity=0.55)
        svg.line(bx0, scale_y.to_px(median), bx1, scale_y.to_px(median), stroke=INK,
                 width=2.0)
        svg.line(center, scale_y.to_px(q3), center, scale_y.to_px(whisker_high),
                 stroke=INK)
        svg.line(center, scale_y.to_px(q1), center, scale_y.to_px(whisker_low),
                 stroke=INK)
        cap = box_w * 0.3
        svg.line(center - cap, scale_y.to_px(whisker_high), center + cap,
                 scale_y.to_px(whisker_high), stroke=INK)
        svg.line(center - cap, scale_y.to_px(whisker_low), center + cap,
                 scale_y.to_px(whisker_low), stroke=INK)

        # 确定性抖动，避免随机数影响可复现性
        for index, value in enumerate(values):
            offset = ((index * 37) % 21 - 10) / 10.0 * (box_w * 0.42)
            svg.circle(center + offset, scale_y.to_px(value), 2.2, "#333333", opacity=0.5)
            if value < lower_limit or value > upper_limit:
                svg.circle(center + offset, scale_y.to_px(value), 3.6, "none",
                           stroke="#333333")

        svg.text(center, inner_bottom + 18, method, size=12, anchor="middle")

    svg.line(inner_left, inner_bottom, inner_right, inner_bottom, stroke=INK, width=1.2)
    svg.line(inner_left, inner_top, inner_left, inner_bottom, stroke=INK, width=1.2)
    panel_titles(svg, box, "剪枝率分布（相对 BFS）", "启发式策略", "剪枝率（%）")


def build_figure(rows: list[dict], csv_name: str, methods: list[str]) -> str:
    svg = Svg(CANVAS_W, CANVAS_H)
    svg.parts.append(
        f'<text x="{CANVAS_W / 2}" y="34" font-size="17" font-weight="bold" '
        f'text-anchor="middle" fill="{INK}">'
        f'{escape(f"8 数码 A* 实验：三种策略性能对比（数据 {csv_name}，共 {len(rows)} 行）")}'
        f'</text>'
    )

    draw_scatter(svg, panel_box(0), rows, "time_ms", "搜索耗时 vs 最优路径长度（对数纵轴）",
                 "搜索耗时（ms）", methods)
    draw_scatter(svg, panel_box(1), rows, "expanded", "扩展节点数 vs 最优路径长度（对数纵轴）",
                 "扩展节点数（个）", methods)
    draw_bars(svg, panel_box(2), rows, methods)
    # 剪枝率的 BFS 基准恒为 0，纳入会把纵轴拉到 0，因此只比较 h1 与 h2
    draw_boxplot(svg, panel_box(3), rows, [m for m in ("h1", "h2") if m in methods])
    return svg.render()


# --------------------------------------------------------------- PNG 栅格化

def find_rasterizer() -> list[str] | None:
    """返回可用的 SVG→PNG 命令模板（{src}/{dst}/{width} 为占位符）。"""
    if shutil.which("rsvg-convert"):
        return ["rsvg-convert", "-w", "{width}", "-o", "{dst}", "{src}"]
    if shutil.which("inkscape"):
        return ["inkscape", "{src}", "-w", "{width}", "-o", "{dst}"]
    if shutil.which("magick"):
        return ["magick", "-background", "white", "-density", "100", "{src}", "{dst}"]
    if shutil.which("convert"):
        return ["convert", "-background", "white", "-density", "100", "{src}", "{dst}"]
    return None


def rasterize(svg_path: Path, png_path: Path, width: int) -> bool:
    template = find_rasterizer()
    if template is None:
        print("提示：系统未找到 rsvg-convert / inkscape / ImageMagick，只输出 SVG。",
              file=sys.stderr)
        print("      SVG 可直接插入 Word，或用浏览器打开后截图。", file=sys.stderr)
        return False
    cmd = [part.format(src=str(svg_path), dst=str(png_path), width=str(width))
           for part in template]
    try:
        subprocess.run(cmd, check=True, capture_output=True)
    except (subprocess.CalledProcessError, OSError) as exc:
        print(f"提示：{cmd[0]} 转换失败（{exc}），只输出 SVG。", file=sys.stderr)
        return False
    return True


# -------------------------------------------------------------------- 入口

def main() -> int:
    parser = argparse.ArgumentParser(
        description="从指标 CSV 生成 8 数码 A* 实验四联图（纯标准库，无需第三方包）"
    )
    parser.add_argument("--csv", default=str(PROJECT_DIR / "results.csv"),
                        help="指标 CSV 路径（默认 ../results.csv）")
    parser.add_argument("--out", default=str(SCRIPT_DIR / "out" / "analysis.png"),
                        help="输出路径（默认 scripts/out/analysis.png）")
    parser.add_argument("--width", type=int, default=CANVAS_W,
                        help="PNG 栅格宽度（默认 1400）")
    args = parser.parse_args()

    csv_path = Path(args.csv).expanduser().resolve()
    out_path = Path(args.out).expanduser().resolve()
    if not csv_path.is_file():
        print(f"找不到数据文件：{csv_path}", file=sys.stderr)
        print("请先生成：./bin/puzzle -b 50 --seed 20261020 --csv results.csv", file=sys.stderr)
        return 1

    try:
        rows = load_rows(csv_path)
    except (ValueError, KeyError) as exc:
        print(f"读取 CSV 失败：{exc}", file=sys.stderr)
        return 1

    methods = [m for m in METHOD_ORDER if any(row["method"] == m for row in rows)]
    methods += sorted({row["method"] for row in rows} - set(methods))

    svg_text = build_figure(rows, csv_path.name, methods)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    svg_path = out_path if out_path.suffix.lower() == ".svg" else out_path.with_suffix(".svg")
    svg_path.write_text(svg_text, encoding="utf-8")
    print(f"数据：{csv_path}（{len(rows)} 行）")
    print(f"策略：{', '.join(methods)}")
    print(f"矢量图：{svg_path}")

    if out_path.suffix.lower() == ".png":
        if rasterize(svg_path, out_path, args.width):
            print(f"位图：{out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
