#!/usr/bin/env python3
"""plot_timeline.py — Member 4

Reads a schedstat CSV (produced by user/schedstat_dump.c, captured via
QEMU console redirect) and renders a Gantt-style timeline of context
switches, colored two ways:
  1. by tgid   -> process-vs-thread view (which process/thread group
                  was on which CPU, over time)
  2. by queue_level ("priority" column) -> scheduler-behavior view
                  (how MLFQ level moves over time)

Each CSV row is one context-switch record: (tick, tgid, tid,
is_thread, state, cpu_id, priority). Since the ring buffer only
records "proc X was dispatched at tick T on cpu C", we don't know the
exact tick it stopped running -- we approximate each bar's width as
"runs until the next record on the same cpu_id begins" (falling back
to a 1-tick sliver for the very last record on a core).

Usage:
    python3 plot_timeline.py schedstat.csv [output.png] [xmin xmax]

If output.png is omitted, the plot is shown interactively instead of
saved (falls back to saving as timeline.png if no display is
available). xmin/xmax optionally crop the view to a tick range (e.g.
to skip a long idle tail after your test finishes) without needing
to edit the CSV.

--- Getting a CSV out of QEMU ---
1. make qemu-nox 2>&1 | tee console.log
2. Inside xv6: run your test, then `schedstat_dump` (no argument
   prints to the console; a filename argument saves inside xv6's own
   fs.img instead, which your HOST machine can't read directly).
3. Extract just the data lines on the host with:
       grep -E '^[0-9]+,[0-9]+,[0-9]+,[0-9]+,[0-9]+,[0-9]+,[0-9]+$' console.log > schedstat.csv
   Prefer this grep over matching the header line with sed -- on a
   multi-core run, another CPU's kernel log output can interleave
   into the middle of the header line and corrupt it, which makes a
   header-anchored sed range silently extract nothing. Matching the
   plain 7-integer data-line shape instead survives that. This
   script already falls back to the correct column names when no
   header line is present, so a headerless CSV from this grep works
   with no extra steps.
"""
import re
import sys
import pandas as pd
import matplotlib
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

STATE_NAMES = {0: "UNUSED", 1: "USED", 2: "SLEEPING",
               3: "RUNNABLE", 4: "RUNNING", 5: "ZOMBIE"}

REQUIRED_COLS = ["tick", "tgid", "tid", "is_thread", "state", "cpu_id", "priority"]
_ROW_RE = re.compile(r"^-?\d+(,-?\d+){6}$")

# A CPU that's genuinely idle (nothing RUNNABLE) produces no schedstat
# records at all -- the ring buffer only logs a dispatch, never "went
# idle". Without a cap, build_bars() would stretch the last real bar
# all the way to the next unrelated event (sometimes hundreds of ticks
# later, e.g. the next time you happen to run schedstat_dump), which
# LOOKS like that process kept running the whole time when it didn't.
# Any gap longer than this is rendered as an explicit "idle" block
# instead of silently extending the previous color.
IDLE_SENTINEL = "__IDLE__"
MAX_BAR_TICKS = 20
IDLE_COLOR = "#d9d9d9"


def load_schedstat_csv(path):
    """Parse a schedstat CSV that may be a raw QEMU console capture rather
    than a clean file -- kernel printk() debug lines (mlfq:, donate:, etc.)
    can land interleaved with user printf() output on the same UART,
    sometimes splicing into the middle of a data line. Rather than trust
    the whole block, validate each line against the exact
    "7 comma-separated integers" shape and silently drop anything that
    doesn't match, so one corrupted/interleaved line is lost instead of
    breaking the whole parse."""
    header = None
    rows = []
    skipped = 0
    with open(path) as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            if line.startswith("tick,"):
                header = line.split(",")
                continue
            if line == "END_SCHEDSTAT":
                continue
            if _ROW_RE.match(line):
                rows.append([int(x) for x in line.split(",")])
            else:
                skipped += 1
    if skipped:
        print(f"plot_timeline: skipped {skipped} non-data line(s) "
              f"(console noise / interleaved kernel output)")
    if not rows:
        print("plot_timeline: no valid schedstat rows found in "
              f"{path} -- did you run schedstat_dump (not donatetest) "
              "and capture its CSV block?")
        sys.exit(1)
    return pd.DataFrame(rows, columns=header or REQUIRED_COLS)


def build_bars(df, cpu_col, value_col):
    """For each cpu_id, turn consecutive (tick, value) rows into
    (start, width, value) bars: each bar runs from its own tick to the
    next record's tick on that same cpu (or a 1-tick sliver for the
    very last record, since there's no later event to bound it).

    If that gap exceeds MAX_BAR_TICKS, the CPU almost certainly went
    idle in between rather than running the same thing the whole
    time -- cap the real bar at MAX_BAR_TICKS and fill the remainder
    with an explicit IDLE_SENTINEL bar instead of stretching color
    over a gap with no supporting data."""
    bars_per_cpu = {}
    for cpu, group in df.groupby(cpu_col):
        group = group.sort_values("tick")
        ticks = group["tick"].to_numpy()
        values = group[value_col].to_numpy()
        bars = []
        for i in range(len(ticks)):
            start = ticks[i]
            if i + 1 < len(ticks):
                gap = ticks[i + 1] - ticks[i]
            else:
                gap = 1  # last record on this cpu: no future event to bound it
            if gap > MAX_BAR_TICKS:
                bars.append((start, MAX_BAR_TICKS, values[i]))
                bars.append((start + MAX_BAR_TICKS, gap - MAX_BAR_TICKS, IDLE_SENTINEL))
            else:
                bars.append((start, max(gap, 1), values[i]))
        bars_per_cpu[cpu] = bars
    return bars_per_cpu


def plot_view(ax, df, cpu_col, value_col, title, cmap_name):
    bars_per_cpu = build_bars(df, cpu_col, value_col)
    cpus = sorted(bars_per_cpu.keys())
    values = sorted(df[value_col].unique())
    cmap = matplotlib.colormaps.get_cmap(cmap_name)
    color_for = {v: cmap(i / max(len(values) - 1, 1)) for i, v in enumerate(values)}

    for row, cpu in enumerate(cpus):
        for start, width, value in bars_per_cpu[cpu]:
            if value == IDLE_SENTINEL:
                ax.broken_barh([(start, width)], (row - 0.4, 0.8),
                                facecolors=IDLE_COLOR, edgecolors="black",
                                linewidth=0.3, hatch="//")
            else:
                ax.broken_barh([(start, width)], (row - 0.4, 0.8),
                                facecolors=color_for[value], edgecolors="black",
                                linewidth=0.3)

    ax.set_yticks(range(len(cpus)))
    ax.set_yticklabels([f"cpu{c}" for c in cpus])
    ax.set_xlabel("tick")
    ax.set_title(title)
    ax.grid(True, axis="x", alpha=0.3)

    patches = [mpatches.Patch(color=color_for[v], label=str(v)) for v in values]
    patches.append(mpatches.Patch(facecolor=IDLE_COLOR, edgecolor="black",
                                    hatch="//", label="idle (no data)"))
    ax.legend(handles=patches, title=value_col, bbox_to_anchor=(1.01, 1),
               loc="upper left", fontsize="small")


def main():
    if len(sys.argv) < 2:
        print("usage: plot_timeline.py <schedstat.csv> [output.png] [xmin xmax]")
        sys.exit(1)

    csv_path = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else None
    xlim = None
    if len(sys.argv) > 4:
        xlim = (int(sys.argv[3]), int(sys.argv[4]))

    df = load_schedstat_csv(csv_path)
    missing = set(REQUIRED_COLS) - set(df.columns)
    if missing:
        print(f"plot_timeline: CSV missing columns: {missing}")
        sys.exit(1)

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 6), sharex=True)
    plot_view(ax1, df, "cpu_id", "tgid",
              "Process/thread-group timeline (color = tgid)", "tab20")
    plot_view(ax2, df, "cpu_id", "priority",
              "Scheduler behavior (color = MLFQ queue_level)", "viridis")
    if xlim:
        ax1.set_xlim(*xlim)
        ax2.set_xlim(*xlim)
    fig.tight_layout()

    if out_path:
        fig.savefig(out_path, dpi=150)
        print(f"plot_timeline: wrote {out_path}")
    else:
        try:
            plt.show()
        except Exception:
            fig.savefig("timeline.png", dpi=150)
            print("plot_timeline: no display available, wrote timeline.png")


if __name__ == "__main__":
    main()