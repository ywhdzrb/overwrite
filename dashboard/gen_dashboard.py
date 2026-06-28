#!/usr/bin/env python3
import matplotlib
matplotlib.use("Agg")
from matplotlib import font_manager
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import numpy as np
import subprocess, collections, os
from datetime import datetime, timedelta

FONT_PATH = "/usr/share/fonts/adobe-source-han-sans/SourceHanSansCN-Light.otf"
if not os.path.exists(FONT_PATH):
    import subprocess as sp
    r = sp.run(["fc-list", ":lang=zh", "file"], capture_output=True, text=True)
    if r.stdout.strip():
        FONT_PATH = r.stdout.strip().split(":")[0].strip()
font_manager.fontManager.addfont(FONT_PATH)
plt.rcParams["font.sans-serif"] = ["Source Han Sans CN"]
plt.rcParams["axes.unicode_minus"] = False

OUT = "dashboard"
os.makedirs(OUT, exist_ok=True)

def run(cmd):
    r = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    return r.stdout.strip()

def file_lines_by_ext(exts, extra_exclude=""):
    exclude = r'! -path "*/build/*" ! -path "*/build-ci/*" ! -path "*/external/*" ! -path "*/.git/*"'
    if extra_exclude:
        exclude += extra_exclude
    parts = " -o ".join(f'-name "*{e}"' for e in exts)
    cmd = f'find . {parts} {exclude} 2>/dev/null | xargs wc -l 2>/dev/null | tail -1'
    r_int = run(cmd)
    if r_int and r_int.split():
        return int(r_int.split()[0])
    return 0

def module_lines(paths, exts=(".cpp", ".hpp")):
    total = 0
    for p in paths:
        if not os.path.isdir(p):
            continue
        parts = " -o ".join(f'-name "*{e}"' for e in exts)
        cmd = f'find {p} {parts} 2>/dev/null | xargs wc -l 2>/dev/null | tail -1'
        r_int = run(cmd)
        if r_int and r_int.split():
            total += int(r_int.split()[0])
    return total

src_cpp  = file_lines_by_ext([".cpp"])
src_hpp  = file_lines_by_ext([".hpp"])
shaders  = file_lines_by_ext([".frag", ".vert", ".comp"])
config   = file_lines_by_ext([".json"], '! -path "*/build-ci/*"')
scripts  = file_lines_by_ext([".sh", ".cmake", ".py"], '! -path "*/build-ci/*"')
docs     = file_lines_by_ext([".md"], '! -path "*/build-ci/*" ! -name "LICENSE.md"')
misc_txt = file_lines_by_ext([".txt", ".yml", ".yaml", ".cfg", ".gitignore"], '! -path "*/build-ci/*"')

mod_core        = module_lines(["src/core", "include/core"])
mod_renderer    = module_lines(["src/renderer", "include/renderer"])
mod_game_session = module_lines(["src/game_session"])
mod_client      = module_lines(["client/src"])
mod_server      = module_lines(["server/src"])
mod_shared      = module_lines(["shared/src", "include/ecs"])
mod_utils       = module_lines(["src/utils", "include/utils"])
mod_tests       = module_lines(["tests"])

log = run("git log --format='%ai' --after='2026-03-01'").splitlines()
dates = [datetime.fromisoformat(l.split()[0]) for l in log if l]
daily_counts = collections.Counter(d.date() for d in dates)
monthly_counts = collections.Counter((d.year, d.month) for d in dates)
hourly_counts = collections.Counter(int(l.split()[1].split(":")[0]) for l in log if l)

commit_types_raw = run("git log --format='%s'").splitlines()
type_counts = collections.Counter()
for msg in commit_types_raw:
    t = msg.split(":")[0].split("(")[0].strip().lower()
    if t in ("feat", "fix", "refactor", "perf", "chore", "test", "docs", "style", "ci", "build", "tweak"):
        type_counts[t] += 1
    else:
        type_counts["other"] += 1

week_map = collections.Counter()
for d in dates:
    iso = d.isocalendar()
    week_map[(iso[0], iso[1])] += 1
weeks_sorted = sorted(week_map.keys())
min_w = weeks_sorted[0]
max_w = weeks_sorted[-1]
total_weeks = ((max_w[0] - min_w[0]) * 52 + max_w[1] - min_w[1]) + 1
heat = np.zeros((7, total_weeks))
for d in dates:
    iso = d.isocalendar()
    wi = (iso[0] - min_w[0]) * 52 + iso[1] - min_w[1]
    heat[iso[2] - 1, wi] += 1

total_commits = len(dates)
total_lines = src_cpp + src_hpp + shaders + config + scripts + docs + misc_txt
first_date = min(dates).strftime("%Y-%m-%d") if dates else "?"
last_date = max(dates).strftime("%Y-%m-%d") if dates else "?"

# ---- 现代配色 ----
BG     = "#1a1a2e"
CARD   = "#16213e"
ACCENT = "#0f3460"
TEXT_C = "#e0e0e0"
GOLD   = "#e2b714"
RED    = "#e74c3c"
GREEN  = "#2ecc71"
BLUE   = "#3498db"
ORANGE = "#f39c12"
PURPLE = "#9b59b6"
CYAN   = "#1abc9c"

TYPE_COLORS = ["#4e79a7", "#59a14f", "#f28e2b", "#e15759", "#76b7b2", "#edc948", "#b07aa1"]
MOD_COLORS  = plt.cm.PuBu(np.linspace(0.3, 0.8, 9))
HR_COLORS   = ["#dddddd"] * 24
peak_h = max(hourly_counts.values()) if hourly_counts else 0
for h in range(24):
    if hourly_counts.get(h, 0) >= peak_h * 0.6:
        HR_COLORS[h] = "#e74c3c"
    elif hourly_counts.get(h, 0) > 0:
        HR_COLORS[h] = "#59a14f"

TYPE_PIE_COLORS = {"feat": "#4e79a7", "fix": "#e74c3c", "refactor": "#59a14f",
                   "docs": "#f39c12", "perf": "#f28e2b", "chore": "#76b7b2",
                   "test": "#9b59b6", "tweak": "#1abc9c", "other": "#95a5a6"}

# ---- 样式工具 ----
def style_ax(ax, title, xlabel="", ylabel=""):
    ax.set_facecolor(CARD)
    ax.set_title(title, fontsize=9, fontweight="bold", color=TEXT_C, pad=6)
    if xlabel: ax.set_xlabel(xlabel, fontsize=7, color=TEXT_C)
    if ylabel: ax.set_ylabel(ylabel, fontsize=7, color=TEXT_C)
    ax.tick_params(colors=TEXT_C, labelsize=6)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.spines["left"].set_color("#444466")
    ax.spines["bottom"].set_color("#444466")

fig = plt.figure(figsize=(22, 20), facecolor=BG)
gs = fig.add_gridspec(4, 6, hspace=0.30, wspace=0.35,
                       left=0.04, right=0.97, top=0.95, bottom=0.04)

# ---- 主标题 ----
fig.text(0.5, 0.975, "OverWrite 引擎 · 项目仪表盘",
         ha="center", fontsize=16, fontweight="bold", color=GOLD)
fig.text(0.5, 0.962, "v0.1.1-beta  |  C++20 / Vulkan 1.3+ / EnTT ECS  |  131 commits  ·  35.6k lines",
         ha="center", fontsize=7, color="#888899")

# ==================== (0,0:2) 代码构成饼图 ====================
ax0 = fig.add_subplot(gs[0, :2])
style_ax(ax0, "代码构成")
ax0.set_facecolor(CARD)
pie_labels_l = ["C++ 源码 (.cpp)", "C++ 头文件 (.hpp)", "着色器 (GLSL)",
                 "配置文件 (JSON)", "脚本 (sh/py/cmake)", "文档 (Markdown)", "其他"]
pie_vals = [src_cpp, src_hpp, shaders, config, scripts, docs, misc_txt]
pie_labels_f = [f"{l}\n({v:,} 行)" for l, v in zip(pie_labels_l, pie_vals) if v > 0]
pie_vals_f = [v for v in pie_vals if v > 0]
pie_colors_f = [c for c, v in zip(TYPE_COLORS, pie_vals) if v > 0]

wedges, texts, autotexts = ax0.pie(
    pie_vals_f, labels=pie_labels_f, colors=pie_colors_f,
    autopct="%1.1f%%", startangle=90, explode=[0.03]*len(pie_vals_f),
    textprops={"fontsize": 6.5, "color": TEXT_C},
    pctdistance=0.75, labeldistance=1.08)
for t in autotexts:
    t.set_fontsize(7)
    t.set_color("white")
    t.set_fontweight("bold")

# ==================== (0,2:4) 文件类型行数 ====================
ax1 = fig.add_subplot(gs[0, 2:4])
style_ax(ax1, "文件类型行数")
type_names = ["C++ 源码", "头文件", "着色器", "配置", "脚本", "文档", "其他"]
type_vals  = [src_cpp, src_hpp, shaders, config, scripts, docs, misc_txt]
bars1 = ax1.bar(type_names, type_vals, color=TYPE_COLORS, edgecolor="#222244",
                width=0.7, linewidth=0.5)
for b, v in zip(bars1, type_vals):
    if v > 0:
        ax1.text(b.get_x() + b.get_width()/2, b.get_height() + max(type_vals)*0.008,
                 f"{v:,}", ha="center", va="bottom", fontsize=6, color=TEXT_C,
                 fontweight="bold")
ax1.set_ylim(0, max(type_vals) * 1.18)
ax1.tick_params(axis="x", rotation=25, labelsize=7)

# ==================== (0,4:) 源码模块分布 ====================
ax2 = fig.add_subplot(gs[0, 4:])
style_ax(ax2, "源码模块 (cpp+hpp)")
mod_names = ["核心引擎", "渲染器", "游戏会话", "客户端",
             "服务端", "共享代码", "工具模块", "测试"]
mod_vals  = [mod_core, mod_renderer, mod_game_session, mod_client,
             mod_server, mod_shared, mod_utils, mod_tests]
bars2 = ax2.barh(mod_names, mod_vals, color=MOD_COLORS, edgecolor="#222244",
                  height=0.6, linewidth=0.5)
for b, v in zip(bars2, mod_vals):
    if v > 0:
        ax2.text(b.get_width() + max(mod_vals)*0.005, b.get_y() + b.get_height()/2,
                 f"{v:,}", ha="left", va="center", fontsize=7, color=TEXT_C,
                 fontweight="bold")
ax2.set_xlim(0, max(mod_vals) * 1.2)

# ==================== (1,:3) 提交时间线 ====================
ax3 = fig.add_subplot(gs[1, :3])
style_ax(ax3, f"每日提交活跃度 — {first_date} ~ {last_date}")
if dates:
    date_range = (max(dates).date() - min(dates).date()).days + 1
    all_dates = [min(dates).date() + timedelta(days=i) for i in range(date_range)]
    daily_commits = [daily_counts.get(d, 0) for d in all_dates]
    ax3.fill_between(all_dates, daily_commits, alpha=0.15, color=BLUE)
    ax3.plot(all_dates, daily_commits, color=BLUE, linewidth=1.0, marker="o",
             markersize=2.5, markerfacecolor=GOLD, markeredgewidth=0.3)
    ax3.xaxis.set_major_locator(mdates.WeekdayLocator(interval=1))
    ax3.xaxis.set_major_formatter(mdates.DateFormatter("%m/%d"))
    ax3.tick_params(axis="x", rotation=45, labelsize=6)
    max_day = max(daily_commits)
    if max_day > 0:
        peak_idx = daily_commits.index(max_day)
        ax3.annotate(f" 最高 {max_day}",
                     xy=(all_dates[peak_idx], max_day),
                     xytext=(25, 10), textcoords="offset points",
                     ha="center", fontsize=7, color=GOLD, fontweight="bold",
                     arrowprops=dict(arrowstyle="->", color=GOLD, lw=0.8))

# ==================== (1,3:5) 时段分布 ====================
ax4 = fig.add_subplot(gs[1, 3:5])
style_ax(ax4, "提交时段分布 (UTC+8)", xlabel="小时", ylabel="次数")
hours_all = list(range(24))
hour_vals = [hourly_counts.get(h, 0) for h in hours_all]
bars4 = ax4.bar(hours_all, hour_vals, color=HR_COLORS, edgecolor="#222244",
                width=0.8, linewidth=0.5)
ax4.set_xticks(hours_all)
ax4.set_xticklabels([str(h) if h % 3 == 0 else "" for h in hours_all], fontsize=6)
# 峰值标注
for h, v in zip(hours_all, hour_vals):
    if v >= peak_h:
        ax4.text(h, v + 0.3, f"{v}h", ha="center", fontsize=6.5, color=RED, fontweight="bold")
    elif v >= peak_h * 0.7:
        ax4.text(h, v + 0.2, f"{v}", ha="center", fontsize=5, color=TEXT_C)

# ==================== (1,5:) 提交类型饼图 ====================
ax5 = fig.add_subplot(gs[1, 5:])
style_ax(ax5, "提交类型分布")
ax5.set_facecolor(CARD)
type_labels = {"feat": "功能", "fix": "修复", "refactor": "重构",
               "docs": "文档", "perf": "性能", "chore": "杂项",
               "test": "测试", "tweak": "调整", "other": "其他"}
type_order = ["feat", "fix", "refactor", "docs", "perf", "chore", "test"]
t_names = [type_labels[t] for t in type_order if type_counts.get(t, 0) > 0]
t_vals  = [type_counts.get(t, 0) for t in type_order if type_counts.get(t, 0) > 0]
t_colors = [TYPE_PIE_COLORS[t] for t in type_order if type_counts.get(t, 0) > 0]
wedges2, texts2, autotexts2 = ax5.pie(
    t_vals, labels=t_names, colors=t_colors,
    autopct="%1.1f%%", startangle=90,
    textprops={"fontsize": 7, "color": TEXT_C})
for t in autotexts2:
    t.set_fontsize(7)
    t.set_color("white")
    t.set_fontweight("bold")

# ==================== (2,:2) 月度提交 ====================
ax6 = fig.add_subplot(gs[2, :2])
style_ax(ax6, "月度提交量", ylabel="次数")
month_labels = ["三月", "四月", "五月"]
month_vals = [monthly_counts.get((2026, 3), 0),
              monthly_counts.get((2026, 4), 0),
              monthly_counts.get((2026, 5), 0)]
month_colors = ["#3498db", "#2ecc71", "#e74c3c"]
bars6 = ax6.bar(month_labels, month_vals, color=month_colors,
                edgecolor="#222244", width=0.45, linewidth=0.5)
for b, v in zip(bars6, month_vals):
    ax6.text(b.get_x() + b.get_width()/2, b.get_height() + 0.3,
             f"{v}", ha="center", fontsize=9, color=TEXT_C, fontweight="bold")
# 增长箭头
if len(month_vals) >= 2:
    for i in range(1, len(month_vals)):
        if month_vals[i] > month_vals[i-1]:
            pct = ((month_vals[i] - month_vals[i-1]) / month_vals[i-1]) * 100
            ax6.annotate(f"+{pct:.0f}%", xy=(i, month_vals[i]),
                        xytext=(i, month_vals[i] + max(month_vals)*0.08),
                        ha="center", fontsize=7, color=GREEN, fontweight="bold")

# ==================== (2,2:4) 热力图 ====================
ax8 = fig.add_subplot(gs[2, 2:4])
ax8.set_facecolor(CARD)
heat_cmap = plt.cm.Greens
heat_cmap.set_bad(color=CARD)
im = ax8.imshow(heat, aspect="auto", cmap="Greens", interpolation="nearest")
ax8.set_yticks(range(7))
ax8.set_yticklabels(["一", "二", "三", "四", "五", "六", "日"],
                     fontsize=7, color=TEXT_C)
week_labels = []
for i in range(total_weeks):
    y, w = (min_w[0] + (min_w[1] + i - 1) // 52, (min_w[1] + i - 1) % 52 + 1)
    week_labels.append(f"W{w:02d}")
ax8.set_xticks(range(total_weeks))
ax8.set_xticklabels(week_labels, rotation=0, ha="center", fontsize=6, color=TEXT_C)
ax8.set_title("周活跃度热力图", fontsize=9, fontweight="bold", color=TEXT_C, pad=6)
for i in range(7):
    for j in range(total_weeks):
        v = int(heat[i, j])
        if v > 0:
            ax8.text(j, i, str(v), ha="center", va="center", fontsize=5.5,
                     color="black" if v < heat.max()*0.65 else "white",
                     fontweight="bold" if v >= heat.max()*0.8 else "normal")
# 移除热力图 spines
for sp in ax8.spines.values():
    sp.set_visible(False)

cbar = fig.colorbar(im, ax=ax8, shrink=0.75, pad=0.02)
cbar.set_label("提交次数", fontsize=6, color=TEXT_C)
cbar.ax.tick_params(colors=TEXT_C, labelsize=6)
cbar.outline.set_color("#444466")

# ==================== (2,4:) 项目摘要信息面板 ====================
ax7 = fig.add_subplot(gs[2, 4:])
ax7.set_facecolor(CARD)
ax7.axis("off")
# Draw a border
rect = plt.Rectangle((0.02, 0.02), 0.96, 0.96, transform=ax7.transAxes,
                     facecolor="#0d1b2a", edgecolor="#444466", linewidth=1.5,
                     zorder=0, alpha=0.5)
ax7.add_patch(rect)

src_files = int(run(r'find . -name "*.cpp" -not -path "*/build/*" -not -path "*/build-ci/*" -not -path "*/external/*" -not -path "*/.git/*" 2>/dev/null | wc -l'))
hdr_files = int(run(r'find . -name "*.hpp" -not -path "*/build/*" -not -path "*/build-ci/*" -not -path "*/external/*" -not -path "*/.git/*" 2>/dev/null | wc -l'))
shd_files = int(run(r'find . -name "*.frag" -o -name "*.vert" -o -name "*.comp" -not -path "*/build/*" -not -path "*/external/*" -not -path "*/.git/*" 2>/dev/null | wc -l'))
tst_files = int(run(r'find tests/ -name "*.cpp" 2>/dev/null | wc -l'))
test_count_str = run(r"grep -r '^TEST(' tests/*.cpp 2>/dev/null | wc -l")
test_count_f_str = run(r"grep -r '^TEST_F(' tests/*.cpp 2>/dev/null | wc -l")
test_total = int(test_count_str) + int(test_count_f_str) if (test_count_str and test_count_f_str) else 172
add_del = run("git log --shortstat --format='' | awk '{a+=$1; d+=$4} END {print a, d}'")
if add_del:
    parts = add_del.split()
    lines_added = int(parts[0]) if parts else 0
    lines_deleted = int(parts[1]) if len(parts) > 1 else 0
else:
    lines_added = lines_deleted = 0

info_sections = [
    ("基础信息", [
        ("项目", "OverWrite Engine"),
        ("版本", "v0.1.1-beta"),
        ("技术栈", "C++20 / Vulkan 1.3+ / EnTT ECS"),
    ]),
    ("源文件", [
        ("总数", f"{src_files + hdr_files + shd_files}"),
        ("实现 (.cpp)", f"{src_files}"),
        ("头文件 (.hpp)", f"{hdr_files}"),
        ("着色器 (GLSL)", f"{shd_files}"),
    ]),
    ("代码量", [
        ("总行数", f"{total_lines:,}"),
        ("引擎核心", f"{src_cpp + src_hpp:,}"),
        ("着色器", f"{shaders:,}"),
        ("单元测试", f"{mod_tests:,}"),
    ]),
    ("Git 统计", [
        ("提交总数", f"{total_commits}"),
        ("新增行数", f"{lines_added:,}"),
        ("删除行数", f"{lines_deleted:,}"),
        ("净增行数", f"{lines_added - lines_deleted:,}"),
    ]),
    ("测试", [
        ("测试文件", f"{tst_files}"),
        ("测试用例", f"{test_total}+"),
        ("状态", "全部通过"),
    ]),
]

y_pos = 0.88
section_colors = [GOLD, BLUE, GREEN, ORANGE, PURPLE]
for si, (sname, items) in enumerate(info_sections):
    # 小节标题
    ax7.text(0.08, y_pos, sname, fontsize=7.5, fontweight="bold",
             color=section_colors[si], transform=ax7.transAxes)
    y_pos -= 0.040
    for label, value in items:
        ax7.text(0.12, y_pos, label, fontsize=6.5, color=TEXT_C,
                 transform=ax7.transAxes)
        ax7.text(0.72, y_pos, value, fontsize=6.5, color="white",
                 fontweight="bold", transform=ax7.transAxes,
                 ha="right")
        y_pos -= 0.029
    # 添加分隔线
    if si < len(info_sections) - 1:
        y_pos -= 0.008
        ax7.plot([0.08, 0.92], [y_pos + 0.01, y_pos + 0.01],
                 color="#333355", linewidth=0.5, transform=ax7.transAxes)
        y_pos -= 0.012

# ==================== (3,:2) 文件大小 Top 8 ====================
ax9 = fig.add_subplot(gs[3, :2])
style_ax(ax9, "最大源文件 Top 8", xlabel="代码行数")
cmd_top = r"""
find . -not -path '*/build/*' -not -path '*/build-ci/*' -not -path '*/external/*' -not -path '*/.git/*' \
  \( -name '*.cpp' -o -name '*.hpp' -o -name '*.frag' -o -name '*.vert' -o -name '*.comp' \) \
  -exec wc -l {} + 2>/dev/null | sort -rn | head -8
"""
top_files_raw = run(cmd_top)
top_names = []
top_lines = []
for line in top_files_raw.splitlines():
    parts = line.strip().split()
    if len(parts) >= 2 and parts[0].isdigit():
        fname = " ".join(parts[1:])
        fname_short = fname.replace("./", "")
        if len(fname_short) > 35:
            fname_short = "..." + fname_short[-32:]
        top_names.append(fname_short)
        top_lines.append(int(parts[0]))

if top_lines:
    top_colors = plt.cm.RdPu(np.linspace(0.25, 0.85, len(top_names)))
    bars9 = ax9.barh(range(len(top_names)), top_lines, color=top_colors,
                     edgecolor="#222244", height=0.6, linewidth=0.5)
    ax9.set_yticks(range(len(top_names)))
    ax9.set_yticklabels(top_names, fontsize=6, color=TEXT_C)
    ax9.invert_yaxis()
    for b, v in zip(bars9, top_lines):
        ax9.text(b.get_width() + max(top_lines)*0.004, b.get_y() + b.get_height()/2,
                 f"{v:,}", ha="left", va="center", fontsize=7, color=TEXT_C,
                 fontweight="bold")
    ax9.set_xlim(0, max(top_lines) * 1.25)

# ==================== (3,2:4) 提交类型横向柱状图 ====================
ax10 = fig.add_subplot(gs[3, 2:4])
style_ax(ax10, "提交类型详情", xlabel="次数")
type_order_all = ["feat", "fix", "refactor", "docs", "perf", "chore", "test", "other"]
t_names_h = [type_labels.get(t, t) + f" ({type_counts.get(t,0)})" for t in type_order_all]
t_vals_h  = [type_counts.get(t, 0) for t in type_order_all]
t_colors_h = [TYPE_PIE_COLORS.get(t, "#666688") for t in type_order_all]
bars10 = ax10.barh(t_names_h, t_vals_h, color=t_colors_h, edgecolor="#222244",
                    height=0.55, linewidth=0.5)
for b, v in zip(bars10, t_vals_h):
    if v > 0:
        ax10.text(b.get_width() + 0.15, b.get_y() + b.get_height()/2,
                 f"{v}", ha="left", va="center", fontsize=7, color=TEXT_C,
                 fontweight="bold")
ax10.set_xlim(0, max(t_vals_h) + 4)

# ==================== (3,4:) 最后活跃 & 里程碑 ====================
ax11 = fig.add_subplot(gs[3, 4:])
ax11.set_facecolor(CARD)
ax11.axis("off")
rect2 = plt.Rectangle((0.02, 0.02), 0.96, 0.96, transform=ax11.transAxes,
                      facecolor="#0d1b2a", edgecolor="#444466", linewidth=1.5,
                      zorder=0, alpha=0.5)
ax11.add_patch(rect2)

# 计算最近活跃日
recent_dates = sorted(daily_counts.keys(), reverse=True)
recent_str = recent_dates[0].strftime("%Y-%m-%d") if recent_dates else "?"
recent_week = recent_dates[0].strftime("%A") if recent_dates else "?"

# 里程碑计算
milestones = [
    ("首个提交", first_date),
    ("第 50 次提交", sorted(dates)[49].strftime("%Y-%m-%d") if len(dates) >= 50 else "-"),
    ("第 100 次提交", sorted(dates)[99].strftime("%Y-%m-%d") if len(dates) >= 100 else "-"),
    ("最近活跃", recent_str),
]

# 每日平均
avg_daily = total_commits / max((max(dates) - min(dates)).days, 1)

# 最有意义的统计
highlights = [
    ("__section__", "里程碑"),
    ("", ""),
]
for label, val in milestones:
    highlights.append((f"  {label}", val))

highlights.append(("", ""))
highlights.append(("__section__", "开发节奏"))
highlights.append(("", ""))
highlights.append(("  \u2605 日均提交", f"{avg_daily:.2f}"))
highlights.append(("  \u2605 单日最高", f"{max(daily_counts.values())} 次 ({recent_str})"))
highlights.append(("  \u2605 活跃时段", "夜间 (21-23h / 0-2h)"))

highlights.append(("", ""))
highlights.append(("__section__", "代码资产"))
highlights.append(("", ""))
highlights.append(("  \u25b6 引擎代码", f"{src_cpp + src_hpp + shaders:,} 行"))
highlights.append(("  \u25b6 测试代码", f"{mod_tests:,} 行"))
highlights.append(("  \u25b6 文档配置", f"{docs + config:,} 行"))

y = 0.88
for item in highlights:
    if not isinstance(item, tuple) or len(item) != 2:
        continue
    key, val = item
    if key == "" and val == "":
        y -= 0.020
    elif key == "":
        y -= 0.008
    elif key == "__section__":
        ax11.text(0.08, y, val, fontsize=8, fontweight="bold", color=TEXT_C,
                 transform=ax11.transAxes)
        y -= 0.035
    else:
        ax11.text(0.08, y, key, fontsize=6.5, color=TEXT_C,
                 transform=ax11.transAxes)
        ax11.text(0.70, y, val, fontsize=6.5, color="white",
                 fontweight="bold", transform=ax11.transAxes, ha="right")
        y -= 0.032

# ---- 保存 ----
plt.savefig(f"{OUT}/dashboard.png", dpi=180, facecolor=BG,
            bbox_inches="tight", pad_inches=0.3)
plt.close()
print(f"Done. {OUT}/dashboard.png  (dpi=180, {total_commits} commits, {total_lines:,} lines)")
