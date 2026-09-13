#!/usr/bin/env python3
"""plot_timeline.py — Member 4

Reads a schedstat CSV (produced by user/schedstat_dump.c, captured via
QEMU console redirect) and renders a Gantt-style timeline of context
switches, colored two ways:
  1. by tgid   -> process-vs-thread view (which process/thread group
                  was on which CPU, over time)
  2. by queue_level ("priority" column) -> scheduler-behavior view
                  (how MLFQ level moves over time)

Each dispatch is now logged TWICE: once when scheduler() picks a proc
(state=RUNNING), and once when sched() switches it back out (state=
whatever it becomes next -- RUNNABLE/SLEEPING/ZOMBIE, never RUNNING).
On a given cpu these always alternate start, stop, start, stop... for
the SAME tgid/tid, so bars are drawn from the exact start tick to the
exact stop tick -- no guessing, and genuine idle stretches (no bars at
all) show up as real blank/idle gaps rather than being confused with
activity. Older CSVs that only have start records (no stop events)
still work via a capped fallback (see build_bars_legacy).

On top of the timeline, this script can also overlay the actual
donation/starvation events for THIS run -- parsed straight out of a
QEMU console.log passed with --events. Nothing is hardcoded per test:
whatever donate:/mlfq: STARVED lines actually happened in that
specific run are what gets drawn, so the same script adapts to
donatetest, pinv_test, dtest_super, starvetest, etc. without needing a
different chart type per test.

Usage:
    python3 plot_timeline.py schedstat.csv [output.png] [--events console.log] [xmin xmax]

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
   header line is present.
4. To also see donation/starvation events overlaid on the chart, pass
   the SAME console.log with --events:
       python3 plot_timeline.py schedstat.csv out.png --events console.log
"""
import re
import sys
import pandas as pd
import matplotlib
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import matplotlib.lines as mlines

STATE_NAMES = {0: "UNUSED", 1: "USED", 2: "SLEEPING",
               3: "RUNNABLE", 4: "RUNNING", 5: "ZOMBIE"}
RUNNING = 4

REQUIRED_COLS = ["tick", "tgid", "tid", "is_thread", "state", "cpu_id", "priority"]
_ROW_RE = re.compile(r"^-?\d+(,-?\d+){6}$")

# Legacy fallback (only used for old CSVs with start-only records, no
# stop events): a gap longer than this is assumed to be idle time
# rather than the same proc still running, and rendered as an explicit
# "idle" block instead of silently stretching the previous color.
IDLE_SENTINEL = "__IDLE__"
MAX_BAR_TICKS = 20
IDLE_COLOR = "#d9d9d9"

# Event lines worth overlaying -- deliberately NOT the routine "quantum
# used up" demotion lines (those fire constantly and would clutter the
# chart; they're already visible as color changes in the priority
# panel). Only the rare, meaningful ones: donation boost/restore, and
# aging force-promotions.
_BOOST_RE = re.compile(r"donate: t=(\d+) pid=(\d+) BOOST level (\d+) -> (\d+)")
_RESTORE_RE = re.compile(r"donate: t=(\d+) pid=(\d+) RESTORE level (\d+) -> (\d+)")
_STARVED_RE = re.compile(r"mlfq: t=(\d+) pid=(\d+) tid=(\d+) STARVED level (\d+)->0")


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


def extract_events(path):
    """Scan a raw QEMU console.log for donation/starvation lines and
    return them as a flat list of (tick, label, kind) tuples. This is
    what makes the chart "dynamic per test": nothing here is specific
    to any one test program, it just reports whatever actually
    happened in this particular run."""
    events = []
    with open(path) as f:
        for raw in f:
            m = _BOOST_RE.search(raw)
            if m:
                t, pid, frm, to = m.groups()
                events.append((int(t), f"pid{pid} BOOST {frm}->{to}", "boost"))
                continue
            m = _RESTORE_RE.search(raw)
            if m:
                t, pid, frm, to = m.groups()
                events.append((int(t), f"pid{pid} RESTORE {frm}->{to}", "restore"))
                continue
            m = _STARVED_RE.search(raw)
            if m:
                t, pid, tid, frm = m.groups()
                events.append((int(t), f"pid{pid}/tid{tid} aged out", "starved"))
    return sorted(events, key=lambda e: e[0])


def cluster_events(events, window=5):
    """Merge consecutive same-kind events that land within `window`
    ticks of each other into one summary label (e.g. three near-
    identical "RESTORE 3->3" noise events three ticks apart become
    one "3x restore (t=55-58)" label) -- otherwise they just overlap
    into illegible stacked text. Deliberately never merges DIFFERENT
    kinds together, even if close in tick: a real BOOST followed a
    few ticks later by its matching RESTORE is exactly the pairing
    this chart exists to show, so those two stay as separate,
    clearly labeled events no matter how close together they are."""
    if not events:
        return events
    clustered = []
    group = [events[0]]
    for e in events[1:]:
        if e[2] == group[-1][2] and e[0] - group[-1][0] <= window:
            group.append(e)
        else:
            clustered.append(_merge_group(group))
            group = [e]
    clustered.append(_merge_group(group))
    return clustered


def _merge_group(group):
    if len(group) == 1:
        return group[0]
    tick, _, kind = group[0]
    last_tick = group[-1][0]
    label = f"{len(group)}x {kind} (t={tick}-{last_tick})"
    return (tick, label, kind)


def build_bars(df, cpu_col, value_col):
    """Preferred path: pair each RUNNING (start) record with its very
    next record on the same cpu for the same tgid/tid (the stop event
    logged by sched() before switching away) for an EXACT bar. Falls
    back to the old capped-guess approach only if the CSV has no stop
    events at all (state is RUNNING on every row -- an old-format
    capture)."""
    has_stop_events = (df["state"] != RUNNING).any()
    if not has_stop_events:
        return build_bars_legacy(df, cpu_col, value_col)

    bars_per_cpu = {}
    for cpu, group in df.groupby(cpu_col):
        group = group.sort_values("tick").reset_index(drop=True)
        bars = []
        i, n = 0, len(group)
        while i < n:
            row = group.iloc[i]
            if row["state"] == RUNNING:
                start_tick = row["tick"]
                value = row[value_col]
                nxt = group.iloc[i + 1] if i + 1 < n else None
                if (nxt is not None and nxt["state"] != RUNNING
                        and nxt["tgid"] == row["tgid"] and nxt["tid"] == row["tid"]):
                    width = max(nxt["tick"] - start_tick, 1)
                    i += 2
                else:
                    # no matching stop captured (e.g. still running when
                    # the ring buffer was dumped) -- draw a small sliver
                    # rather than guessing how long it ran.
                    width = 1
                    i += 1
                bars.append((start_tick, width, value))
            else:
                # an orphan stop record with no start in this window
                # (can happen right at the start of a capture) -- skip it,
                # it carries no new bar of its own.
                i += 1
        bars_per_cpu[cpu] = bars
    return bars_per_cpu


def build_bars_legacy(df, cpu_col, value_col):
    """Old behavior, kept for CSVs captured before the exact stop-event
    logging existed: each bar runs until the next record on the same
    cpu, capped at MAX_BAR_TICKS with the remainder drawn as idle."""
    bars_per_cpu = {}
    for cpu, group in df.groupby(cpu_col):
        group = group.sort_values("tick")
        ticks = group["tick"].to_numpy()
        values = group[value_col].to_numpy()
        bars = []
        for i in range(len(ticks)):
            start = ticks[i]
            gap = ticks[i + 1] - ticks[i] if i + 1 < len(ticks) else 1
            if gap > MAX_BAR_TICKS:
                bars.append((start, MAX_BAR_TICKS, values[i]))
                bars.append((start + MAX_BAR_TICKS, gap - MAX_BAR_TICKS, IDLE_SENTINEL))
            else:
                bars.append((start, max(gap, 1), values[i]))
        bars_per_cpu[cpu] = bars
    return bars_per_cpu


def plot_view(ax, df, cpu_col, value_col, title, cmap_name, events=None):
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

    EVENT_COLOR = {"boost": "#d62728", "restore": "#2ca02c", "starved": "#9467bd"}
    if events:
        ylim = ax.get_ylim()
        yspan = ylim[1] - ylim[0]
        # Stagger label heights in a repeating pattern so events that
        # land close together in tick don't overlap into illegible
        # stacked text -- each successive event (in tick order) goes
        # one step higher, cycling through a few offset levels.
        n_levels = 4
        for idx, (tick, label, kind) in enumerate(events):
            color = EVENT_COLOR.get(kind, "black")
            ax.axvline(tick, color=color, linestyle="--", linewidth=1, alpha=0.8)
            y = ylim[1] + (idx % n_levels) * 0.55 * yspan
            ax.text(tick, y, label, rotation=90, va="bottom", ha="center",
                    fontsize=7, color=color)

    patches = [mpatches.Patch(color=color_for[v], label=str(v)) for v in values]
    used_idle = any(v == IDLE_SENTINEL for bars in bars_per_cpu.values() for _, _, v in bars)
    if used_idle:
        patches.append(mpatches.Patch(facecolor=IDLE_COLOR, edgecolor="black",
                                        hatch="//", label="idle (no data)"))
    if events:
        seen_kinds = {k for _, _, k in events}
        for kind in ("boost", "restore", "starved"):
            if kind in seen_kinds:
                patches.append(mlines.Line2D([], [], color=EVENT_COLOR[kind],
                                              linestyle="--", label=kind))
    ax.legend(handles=patches, title=value_col, bbox_to_anchor=(1.01, 1),
               loc="upper left", fontsize="small")


def main():
    argv = sys.argv[1:]
    events_path = None
    if "--events" in argv:
        idx = argv.index("--events")
        events_path = argv[idx + 1]
        del argv[idx:idx + 2]

    if len(argv) < 1:
        print("usage: plot_timeline.py <schedstat.csv> [output.png] "
              "[--events console.log] [xmin xmax]")
        sys.exit(1)

    csv_path = argv[0]
    out_path = argv[1] if len(argv) > 1 else None
    xlim = (int(argv[2]), int(argv[3])) if len(argv) > 3 else None

    df = load_schedstat_csv(csv_path)
    missing = set(REQUIRED_COLS) - set(df.columns)
    if missing:
        print(f"plot_timeline: CSV missing columns: {missing}")
        sys.exit(1)

    events = extract_events(events_path) if events_path else None
    if events_path and not events:
        print(f"plot_timeline: --events given but no donate:/STARVED lines "
              f"found in {events_path} (fine for tests like racetest that "
              f"don't exercise donation)")
    if events:
        events = cluster_events(events)

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 6), sharex=True)
    plot_view(ax1, df, "cpu_id", "tgid",
              "Process/thread-group timeline (color = tgid)", "tab20")
    plot_view(ax2, df, "cpu_id", "priority",
              "Scheduler behavior (color = MLFQ queue_level)", "viridis",
              events=events)
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