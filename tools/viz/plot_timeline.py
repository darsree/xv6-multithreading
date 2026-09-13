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
is_thread, state, cpu_id, priority). The kernel now logs TWO records
per run: a "start" record when a proc is dispatched (state==RUNNING)
and a "stop" record at the exact tick it gives the CPU back (state is
whatever it transitioned to -- RUNNABLE/SLEEPING/ZOMBIE -- which is
never RUNNING). That lets this script draw each bar with its real,
exact width instead of guessing, and any gap between a stop record
and the next start record on that cpu is genuine, *measured* idle
time -- not an inferred one.

Older-format CSVs that only contain start records (no matching stop
row right after each start) are still supported: this script falls
back to the old approximation -- "runs until the next record on the
same cpu_id begins," capped at MAX_BAR_TICKS with the remainder drawn
as an explicit idle block -- so you don't need to regenerate old data
to use this script.

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

# LEGACY-MODE ONLY (see build_bars_legacy() below): a CPU that's
# genuinely idle produces no record at all in the old start-only
# format, so a gap has to be guessed at. Any legacy gap longer than
# this is rendered as an explicit "idle" block instead of silently
# extending the previous color as if the CPU ran that long.
#
# FLAW FIX: this used to be 20, but kernel/sched.c's adaptive quantum
# can legitimately grow a single dispatch's run up to
# MLFQ_MAX_QUANTUM == 32 ticks (see the IO_BOUND_PCT growth path in
# mlfq_on_switch_out()). With a 20-tick cap, any genuinely full-speed
# 21-32 tick run got chopped at 20 and the remainder mislabeled idle
# even though the CPU never stopped. Matching the real kernel constant
# here removes that false positive. (This whole class of guesswork is
# now avoided entirely in exact mode -- see build_bars_exact().)
IDLE_SENTINEL = "__IDLE__"
MAX_BAR_TICKS = 32  # == kernel/sched.c MLFQ_MAX_QUANTUM
IDLE_COLOR = "#d9d9d9"
RUNNING_STATE = 4  # enum procstate RUNNING, from kernel/proc.h


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


def build_bars_legacy(ticks, states, values):
    """Old approximation, kept as a fallback for CSVs that only ever
    logged a dispatch ("start") record and never a matching stop
    record. Each bar runs from its own tick to the next record's tick
    on that same cpu (or a 1-tick sliver for the very last record).
    Any gap over MAX_BAR_TICKS is capped and the remainder drawn as an
    explicit idle block, since the CPU almost certainly went idle
    rather than having run the same thing that whole time."""
    bars = []
    for i in range(len(ticks)):
        start = ticks[i]
        gap = ticks[i + 1] - ticks[i] if i + 1 < len(ticks) else 1
        if gap > MAX_BAR_TICKS:
            bars.append((start, MAX_BAR_TICKS, values[i]))
            bars.append((start + MAX_BAR_TICKS, gap - MAX_BAR_TICKS, IDLE_SENTINEL))
        else:
            bars.append((start, max(gap, 1), values[i]))
    return bars


def build_bars_exact(ticks, states, values):
    """Pairs each start record (state == RUNNING) with the very next
    record on the same cpu, which is that run's own stop record (any
    non-RUNNING state, logged from sched() at the exact tick it gives
    up the cpu -- see kernel/proc.c). Bar width is then exact, not
    guessed, and any gap between a stop record and the next start
    record is genuine, measured idle time (not capped/inferred)."""
    bars = []
    i = 0
    n = len(ticks)
    while i < n:
        if states[i] == RUNNING_STATE and i + 1 < n and states[i + 1] != RUNNING_STATE:
            start, end = ticks[i], ticks[i + 1]
            bars.append((start, max(end - start, 1), values[i]))
            i += 2
            # Real, measured idle gap until the next dispatch on this cpu.
            if i < n and ticks[i] > end:
                bars.append((end, ticks[i] - end, IDLE_SENTINEL))
        else:
            # Stray/last record with no pairing available (e.g. a start
            # with nothing after it in the capture window) -- draw a
            # thin sliver rather than guessing a width for it.
            bars.append((ticks[i], 1, values[i]))
            i += 1
    return bars


def build_bars(df, cpu_col, value_col):
    """Dispatches to exact-pairing mode whenever the CSV actually
    contains stop records (any non-RUNNING state), and falls back to
    the legacy single-event approximation for older captures that
    don't."""
    exact_mode = (df["state"] != RUNNING_STATE).any()
    build_fn = build_bars_exact if exact_mode else build_bars_legacy

    bars_per_cpu = {}
    for cpu, group in df.groupby(cpu_col):
        group = group.sort_values("tick")
        ticks = group["tick"].to_numpy()
        states = group["state"].to_numpy()
        values = group[value_col].to_numpy()
        bars_per_cpu[cpu] = build_fn(ticks, states, values)
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