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
    python3 plot_timeline.py schedstat.csv [output.png]

If output.png is omitted, the plot is shown interactively instead of
saved (falls back to saving as timeline.png if no display is
available).
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
    next record's tick on that same cpu (or +1 tick for the last one)."""
    bars_per_cpu = {}
    for cpu, group in df.groupby(cpu_col):
        group = group.sort_values("tick")
        ticks = group["tick"].to_numpy()
        values = group[value_col].to_numpy()
        bars = []
        for i in range(len(ticks)):
            start = ticks[i]
            end = ticks[i + 1] if i + 1 < len(ticks) else ticks[i] + 1
            width = max(end - start, 1)
            bars.append((start, width, values[i]))
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
            ax.broken_barh([(start, width)], (row - 0.4, 0.8),
                            facecolors=color_for[value], edgecolors="black",
                            linewidth=0.3)

    ax.set_yticks(range(len(cpus)))
    ax.set_yticklabels([f"cpu{c}" for c in cpus])
    ax.set_xlabel("tick")
    ax.set_title(title)
    ax.grid(True, axis="x", alpha=0.3)

    patches = [mpatches.Patch(color=color_for[v], label=str(v)) for v in values]
    ax.legend(handles=patches, title=value_col, bbox_to_anchor=(1.01, 1),
               loc="upper left", fontsize="small")


def main():
    if len(sys.argv) < 2:
        print("usage: plot_timeline.py <schedstat.csv> [output.png]")
        sys.exit(1)

    csv_path = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else None

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