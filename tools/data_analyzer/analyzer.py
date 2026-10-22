# -*- coding: utf-8 -*-
"""
通用点位数据分析器

用法:
    python analyzer.py 数据.txt                 # 分析指定文件
    python analyzer.py                          # 弹出文件选择框
    python analyzer.py 数据.txt -o 输出.png     # 指定输出图片
    python analyzer.py 数据.txt --no-report     # 不生成统计报告

输出:
    <文件名>_analysis.png   六联图
    <文件名>_stats.txt      统计报告

支持的 txt 格式(自动识别):
    组1:  (1949.118, 1454.845)  (1946.288, 1478.104)
    1: 1949.118, 1454.845
    1949.118  1454.845
    (1949.118, 1454.845)
    1949.118,1454.845
"""
import argparse
import os
import re
import sys
from pathlib import Path

import numpy as np

# ---------------------------------------------------------------- 数据解析

# 每行开头可能存在的编号前缀: 组1: / 第1组: / 1: / 1. / 1) / 1、
_LABEL_RE = re.compile(r"^\s*(?:组|第|Group|GROUP|group)?\s*\d+\s*(?:组)?\s*[:：.)、]\s*")
_NUM_RE = re.compile(r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")


def parse_points(text):
    """从文本解析出 [[(x,y), ...], ...] 形式的分组点集，同时返回原始行号"""
    groups, linenos, raws = [], [], []
    for lineno, line in enumerate(text.splitlines(), 1):
        s = line.strip()
        if not s or s.startswith("#") or s.startswith("//"):
            continue
        # 统一各种括号
        s = s.replace("[", "(").replace("]", ")").replace("{", "(").replace("}", ")")
        s = _LABEL_RE.sub("", s, count=1)
        nums = _NUM_RE.findall(s)
        if len(nums) < 2:
            continue
        if len(nums) % 2 != 0:
            nums = nums[:-1]           # 丢弃落单的数字
        pts = [(float(nums[i]), float(nums[i + 1])) for i in range(0, len(nums), 2)]
        groups.append(pts)
        linenos.append(lineno)
        raws.append(line.strip())
    return groups, linenos, raws


# 常见企业透明加密软件的文件头标记
_ENCRYPT_MARKS = (b"E-SafeNet", b"E-SAFENET", b"ESafeNet")


def load_data(path):
    raw = Path(path).read_bytes()

    if not raw.strip():
        raise ValueError("文件是空的")

    for mark in _ENCRYPT_MARKS:
        if mark in raw[:8192]:
            raise RuntimeError(
                f"文件被 E-SafeNet（亿赛通）加密，Python 读到的是密文而不是明文数据。\n"
                f"  这就是「点数不一致」的真正原因：密文里的随机数字被当成了坐标。\n\n"
                f"  解决办法（任选其一）：\n"
                f"   1. 在亿赛通客户端里对该文件执行【解密/申请解密】，再分析解密后的文件；\n"
                f"   2. 用已授权的记事本打开 → 全选复制 → 粘贴到新建的 txt，再分析新文件；\n"
                f"   3. 请 IT 把 python.exe / py.exe 加入加密白名单（一劳永逸）；\n"
                f"   4. 直接把数据内容贴给我，我帮你算。"
            )

    # 非加密但也可能是二进制：控制字符比例过高
    head = raw[:8192]
    ctrl = sum(1 for b in head if b < 32 and b not in (9, 10, 13))
    if len(head) and ctrl / len(head) > 0.05:
        raise ValueError(
            "文件看起来不是纯文本（含大量二进制/控制字符），可能仍被加密或选错了文件。\n"
            "  请确认选择的是坐标数据 txt，必要时先解密。"
        )

    for enc in ("utf-8-sig", "utf-8", "gbk", "ansi"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("latin-1")


def normalize(groups, linenos=None, raws=None):
    """校验每组点数一致；以多数行的点数为准，并明确指出不一致的行"""
    counts = [len(g) for g in groups]
    if not counts:
        raise ValueError(
            "没有解析到任何坐标数据。请检查文件内容，或先把文件解密再试"
        )
    # 用众数作为基准（单个异常行不会把整体拉低）
    m = max(set(counts), key=lambda c: (counts.count(c), c))
    bad = [(i, c) for i, c in enumerate(counts) if c != m]
    if bad:
        print(f"[提示] 以多数行的 {m} 个点为准，以下行已跳过：")
        for i, c in bad[:10]:
            ln = linenos[i] if linenos else i + 1
            sample = (raws[i][:70] + "...") if raws and len(raws[i]) > 70 else (raws[i] if raws else "")
            print(f"  第 {ln} 行 → 解析出 {c} 个点   {sample}")
        if len(bad) > 10:
            print(f"  ... 还有 {len(bad) - 10} 行")
        print("  (如果这不是你预期的行数，多半是文件加密/编码或注释里的数字干扰)")
    keep = [i for i, c in enumerate(counts) if c >= m]
    return [groups[i][:m] for i in keep], m


# ---------------------------------------------------------------- 统计分析

def analyze(groups, m):
    n = len(groups)
    arr = np.array(groups, dtype=float)      # (n, m, 2)
    idx = np.arange(n)

    per_point = []
    for k in range(m):
        x = arr[:, k, 0]
        y = arr[:, k, 1]
        center = np.array([x.mean(), y.mean()])
        r = np.linalg.norm(arr[:, k, :] - center, axis=1)
        kx, _ = np.polyfit(idx, x, 1) if n > 1 else (0.0, 0.0)
        ky, _ = np.polyfit(idx, y, 1) if n > 1 else (0.0, 0.0)
        per_point.append({
            "x": x, "y": y, "center": center, "r": r,
            "x_mean": x.mean(), "x_std": x.std(ddof=1) if n > 1 else 0.0,
            "x_min": x.min(), "x_max": x.max(), "x_range": x.max() - x.min(),
            "y_mean": y.mean(), "y_std": y.std(ddof=1) if n > 1 else 0.0,
            "y_min": y.min(), "y_max": y.max(), "y_range": y.max() - y.min(),
            "r_mean": r.mean(), "r_max": r.max(),
            "x_slope": kx, "y_slope": ky,
            "x_outliers": (np.where(np.abs(x - x.mean()) > 3 * x.std(ddof=1))[0] + 1).tolist() if n > 1 and x.std(ddof=1) > 0 else [],
            "y_outliers": (np.where(np.abs(y - y.mean()) > 3 * y.std(ddof=1))[0] + 1).tolist() if n > 1 and y.std(ddof=1) > 0 else [],
        })

    # 组内点间距
    pair_stats = []
    for a in range(m):
        for b in range(a + 1, m):
            d = np.linalg.norm(arr[:, a, :] - arr[:, b, :], axis=1)
            pair_stats.append({
                "label": f"点{a + 1}-点{b + 1}",
                "mean": d.mean(),
                "std": d.std(ddof=1) if n > 1 else 0.0,
                "min": d.min(), "max": d.max(), "range": d.max() - d.min(),
                "data": d,
            })
    return arr, per_point, pair_stats, idx


def build_report(path, groups, m, per_point, pair_stats, arr):
    n = len(groups)
    L = []
    L.append(f"数据文件: {os.path.basename(path)}")
    L.append(f"组数: {n}    每组点数: {m}")
    L.append("=" * 72)
    for k, p in enumerate(per_point):
        L.append(f"\n[点{k + 1}]")
        L.append(f"  X: 均值={p['x_mean']:.4f}  标准差={p['x_std']:.4f}  "
                 f"min={p['x_min']:.3f}  max={p['x_max']:.3f}  极差={p['x_range']:.4f}")
        L.append(f"  Y: 均值={p['y_mean']:.4f}  标准差={p['y_std']:.4f}  "
                 f"min={p['y_min']:.3f}  max={p['y_max']:.3f}  极差={p['y_range']:.4f}")
        L.append(f"  2D偏差(相对均值): 平均={p['r_mean']:.4f}  最大={p['r_max']:.4f}  "
                 f"(最大出现在组 {int(p['r'].argmax()) + 1})")
        L.append(f"  漂移趋势(线性): X {p['x_slope']:+.5f}/组 (累计 {p['x_slope'] * n:+.3f})   "
                 f"Y {p['y_slope']:+.5f}/组 (累计 {p['y_slope'] * n:+.3f})")
        if p["x_outliers"]:
            L.append(f"  X 3σ离群组: {p['x_outliers']}")
        if p["y_outliers"]:
            L.append(f"  Y 3σ离群组: {p['y_outliers']}")

    if pair_stats:
        L.append("\n" + "=" * 72)
        L.append("[组内点间距]")
        for s in pair_stats:
            L.append(f"  {s['label']}: 均值={s['mean']:.3f}  标准差={s['std']:.4f}  "
                     f"min={s['min']:.3f}  max={s['max']:.3f}  极差={s['range']:.4f}")

    # 总体
    L.append("\n" + "=" * 72)
    L.append("[总体]")
    flats = arr.reshape(-1, 2)
    L.append(f"  全部点 X: 均值={flats[:, 0].mean():.4f}  "
             f"标准差={flats[:, 0].std(ddof=1) if len(flats) > 1 else 0:.4f}  "
             f"极差={flats[:, 0].max() - flats[:, 0].min():.4f}")
    L.append(f"  全部点 Y: 均值={flats[:, 1].mean():.4f}  "
             f"标准差={flats[:, 1].std(ddof=1) if len(flats) > 1 else 0:.4f}  "
             f"极差={flats[:, 1].max() - flats[:, 1].min():.4f}")
    return "\n".join(L)


# ---------------------------------------------------------------- 绘图

def make_figure(arr, per_point, pair_stats, idx, title, plt):
    n, m, _ = arr.shape
    colors = plt.cm.tab10(np.linspace(0, 1, max(m, 2)))[:m] if m > 1 else [np.array([0.89, 0.19, 0.19, 1.0])]
    names = [f"点{k + 1}" for k in range(m)]

    fig, axes = plt.subplots(2, 3, figsize=(16, 9))
    fig.suptitle(f"{title}    ({n}组 × {m}点)", fontsize=15, fontweight="bold")

    # 1. 散点
    ax = axes[0, 0]
    for k in range(m):
        ax.scatter(arr[:, k, 0], arr[:, k, 1], s=18, alpha=0.65, c=[colors[k]], label=names[k])
        ax.scatter(per_point[k]["center"][0], per_point[k]["center"][1], s=110,
                   c=[colors[k]], marker="*", edgecolors="k", zorder=5)
    ax.set_xlabel("X"); ax.set_ylabel("Y")
    ax.set_title("散点分布(★=均值)"); ax.legend(); ax.grid(alpha=0.3)

    # 2. 2D 偏差
    ax = axes[0, 1]
    for k in range(m):
        ax.plot(idx + 1, per_point[k]["r"], "o-", ms=3.5, lw=1, c=colors[k], label=names[k])
    ax.set_xlabel("组号"); ax.set_ylabel("偏差半径")
    ax.set_title("各组相对均值的 2D 偏差"); ax.legend(); ax.grid(alpha=0.3)

    # 3. 偏差直方图
    ax = axes[0, 2]
    for k in range(m):
        ax.hist(per_point[k]["r"], bins=min(15, max(5, n // 3)), alpha=0.55,
                color=colors[k], label=names[k])
    ax.set_xlabel("偏差半径"); ax.set_ylabel("频次")
    ax.set_title("2D 偏差分布"); ax.legend(); ax.grid(alpha=0.3)

    # 4. X
    ax = axes[1, 0]
    for k in range(m):
        ax.plot(idx + 1, arr[:, k, 0], "o-", ms=2.5, lw=0.8, c=colors[k], label=f"{names[k]}.X")
        ax.axhline(per_point[k]["x_mean"], c=colors[k], ls="--", alpha=0.5)
    ax.set_xlabel("组号"); ax.set_ylabel("X")
    ax.set_title("X 随组号变化(虚线=均值)"); ax.legend(ncol=2, fontsize=8); ax.grid(alpha=0.3)

    # 5. Y
    ax = axes[1, 1]
    for k in range(m):
        ax.plot(idx + 1, arr[:, k, 1], "o-", ms=2.5, lw=0.8, c=colors[k], label=f"{names[k]}.Y")
        ax.axhline(per_point[k]["y_mean"], c=colors[k], ls="--", alpha=0.5)
    ax.set_xlabel("组号"); ax.set_ylabel("Y")
    ax.set_title("Y 随组号变化(虚线=均值)"); ax.legend(ncol=2, fontsize=8); ax.grid(alpha=0.3)

    # 6. 箱线图
    ax = axes[1, 2]
    data = [arr[:, k, 0] for k in range(m)] + [arr[:, k, 1] for k in range(m)]
    labels = [f"{names[k]}X" for k in range(m)] + [f"{names[k]}Y" for k in range(m)]
    bp = ax.boxplot(data, patch_artist=True, tick_labels=labels)
    for i, patch in enumerate(bp["boxes"]):
        patch.set_facecolor(colors[i % m]); patch.set_alpha(0.45)
    ax.set_title("箱线图"); ax.grid(alpha=0.3, axis="y")
    if m > 1:
        ax.tick_params(axis="x", labelsize=8, rotation=30)

    plt.tight_layout(rect=[0, 0, 1, 0.95])

    # 族图: 组内间距
    if pair_stats:
        fig2, ax2 = plt.subplots(figsize=(10, 3.6))
        for i, s in enumerate(pair_stats):
            ax2.plot(idx + 1, s["data"], "o-", ms=3, lw=1, label=s["label"])
        ax2.set_xlabel("组号"); ax2.set_ylabel("距离")
        ax2.set_title(f"{title} — 组内点间距稳定性"); ax2.legend(ncol=3, fontsize=9); ax2.grid(alpha=0.3)
        plt.tight_layout()
        return fig, fig2
    return fig, None


# ---------------------------------------------------------------- 主流程

def pick_file():
    import tkinter as tk
    from tkinter import filedialog, messagebox
    root = tk.Tk(); root.withdraw()
    p = filedialog.askopenfilename(title="选择数据文件",
                                   filetypes=[("文本文件", "*.txt *.csv *.log"), ("所有文件", "*.*")])
    root.destroy()
    if not p:
        return None
    return p


def main():
    ap = argparse.ArgumentParser(description="通用点位数据分析器")
    ap.add_argument("input", nargs="?", help="输入 txt 文件")
    ap.add_argument("-o", "--output", help="输出 PNG 路径(默认与输入同目录)")
    ap.add_argument("--no-report", action="store_true", help="不生成统计报告 txt")
    ap.add_argument("--show", action="store_true", help="生成后打开图片")
    args = ap.parse_args()

    path = args.input or pick_file()
    if not path:
        print("未选择文件，已退出")
        return 1
    if not os.path.isfile(path):
        print(f"文件不存在: {path}")
        return 1

    try:
        text = load_data(path)
        groups, linenos, raws = parse_points(text)
        groups, m = normalize(groups, linenos, raws)
    except (RuntimeError, ValueError) as e:
        print(f"\n[无法分析] {e}\n")
        return 2
    if len(groups) < 2:
        print("警告: 只有 1 组数据，趋势/标准差意义有限")

    arr, per_point, pair_stats, idx = analyze(groups, m)

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import font_manager
    available = {f.name for f in font_manager.fontManager.ttflist}
    for c in ["Microsoft YaHei", "SimHei", "Noto Sans CJK SC", "Source Han Sans SC"]:
        if c in available:
            plt.rcParams["font.sans-serif"] = [c]
            break
    plt.rcParams["axes.unicode_minus"] = False

    stem = Path(path).stem
    base = Path(path).parent
    out_png = Path(args.output) if args.output else base / f"{stem}_analysis.png"
    fig, fig2 = make_figure(arr, per_point, pair_stats, idx, stem, plt)
    fig.savefig(out_png, dpi=130, bbox_inches="tight")
    extra = []
    if fig2 is not None:
        out_png2 = out_png.with_name(out_png.stem + "_spacing.png")
        fig2.savefig(out_png2, dpi=130, bbox_inches="tight")
        extra.append(str(out_png2))

    report = build_report(path, groups, m, per_point, pair_stats, arr)
    if not args.no_report:
        out_txt = base / f"{stem}_stats.txt"
        out_txt.write_text(report, encoding="utf-8-sig")
        extra.insert(0, str(out_txt))

    print(report)
    print("\n已输出:")
    print(" ", out_png)
    for e in extra:
        print(" ", e)

    if args.show:
        os.startfile(str(out_png))
    return 0


if __name__ == "__main__":
    sys.exit(main())
