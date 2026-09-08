#!/usr/bin/env python3
"""
plot_schedstat.py — Member 4 deliverable: turns getschedstat()'s raw ring
buffer into a Gantt-style timeline + priority-over-time chart.

Usage:
    python3 tools/plot_schedstat.py sched.csv [qemu_session.log] -o out.png

  sched.csv          CSV produced by `schedstat_dump <file>` inside xv6
                      (columns: tick,tgid,tid,is_thread,state,cpu_id,priority)
  qemu_session.log    optional — the raw console capture. If given, the
                      script also pulls out any "mlfq:" / "donate:" printk
                      lines and overlays them as annotations, so MLFQ
                      demotions and priority-donation boosts/restores show
                      up directly on the chart.

Two panels are produced:
  1. Priority (queue_level) vs. tick, one line per thread/tid — this is the
     "changing values" view: you can watch a thread get demoted by MLFQ and
     then see donation pull it back up (or, as here, restore it after a
     mutex is released).
  2. A CPU occupancy Gantt chart: which tid was running on which core over
     time, colored by tgid (so you can see process-vs-thread grouping).

Get sched.csv by running xv6 and, inside the shell:
    $ donatetest
    $ schedstat_dump sched.csv
then after shutting qemu down, extract sched.csv from fs.img (or just copy
the CSV block that schedstat_dump also prints to the console, between the
==SCHEDSTAT_CSV_BEGIN==/END== markers, into a file by hand).
"""
import argparse
import csv
import re
import sys
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches


def load_csv(path):
    rows = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append({k: int(v) for k, v in r.items()})
    return rows


def load_annotations(path):
    """Return (tick, level0, level1, kind) tuples for mlfq:/donate: printk
    lines. donate: lines don't carry their own tick, so we tag them with the
    tick of the most recently seen mlfq: line (events are printed in the
    order they occur on the console)."""
    if not path:
        return []
    annots = []
    last_tick = 0
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("mlfq:"):
                mm = re.search(r"t=(\d+).*?level (\d+)->(\d+)", line)
                if mm:
                    last_tick = int(mm.group(1))
                    annots.append((last_tick, int(mm.group(2)), int(mm.group(3)), "mlfq"))
            elif line.startswith("donate:"):
                mm = re.search(r"(BOOST|RESTORE) level (\d+) ?-> ?(\d+)", line)
                if mm:
                    kind = "donate-" + mm.group(1).lower()
                    annots.append((last_tick, int(mm.group(2)), int(mm.group(3)), kind))
    return annots


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", help="CSV file from schedstat_dump")
    ap.add_argument("log", nargs="?", help="optional raw console log for annotations")
    ap.add_argument("-o", "--out", default="schedstat_timeline.png")
    ap.add_argument("--xlim", nargs=2, type=float, default=None,
                     help="zoom the x-axis, e.g. --xlim 30 55")
    args = ap.parse_args()

    rows = load_csv(args.csv)
    if not rows:
        print("no rows in CSV", file=sys.stderr)
        sys.exit(1)
    annots = load_annotations(args.log)

    tids = sorted(set(r["tid"] for r in rows))
    tgids = sorted(set(r["tgid"] for r in rows))
    cmap = plt.get_cmap("tab10")
    tgid_color = {tg: cmap(i % 10) for i, tg in enumerate(tgids)}

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(11, 7), sharex=True,
                                    gridspec_kw={"height_ratios": [2, 1]})

    # --- Panel 1: priority (queue_level) over time, per tid ---
    for tid in tids:
        pts = [(r["tick"], r["priority"]) for r in rows if r["tid"] == tid]
        pts.sort()
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        tgid = next(r["tgid"] for r in rows if r["tid"] == tid)
        label = f"tid {tid} (tgid {tgid})"
        ax1.step(xs, ys, where="post", marker="o", markersize=3,
                  label=label, color=tgid_color[tgid])

    for (t, l0, l1, kind) in annots:
        is_donate = kind.startswith("donate")
        color = "red" if is_donate else "gray"
        # nudge donate ticks slightly right so they don't overlap an mlfq
        # arrow that happened at the same tick
        xt = t + 0.3 if is_donate else t
        ax1.annotate("", xy=(xt, l1), xytext=(xt, l0),
                     arrowprops=dict(arrowstyle="->", color=color, lw=2 if is_donate else 1.5))
        ax1.plot(xt, l1, marker="x", color=color, markersize=9)
        if is_donate:
            ax1.annotate(kind.replace("-", " "),
                         xy=(xt, l1), xytext=(xt + 1, (l0 + l1) / 2),
                         fontsize=8, color="red")

    ax1.set_ylabel("queue_level (0 = most urgent)")
    ax1.invert_yaxis()
    ax1.set_title("Thread priority (MLFQ queue_level) over time — "
                   "gray arrows = MLFQ demotion/promotion, red = priority donation")
    ax1.legend(loc="upper left", fontsize=8)
    ax1.grid(alpha=0.3)

    # --- Panel 2: Gantt-style CPU occupancy ---
    by_cpu = defaultdict(list)
    for r in rows:
        by_cpu[r["cpu_id"]].append(r)
    cpus = sorted(by_cpu.keys())

    for row_idx, cpu in enumerate(cpus):
        events = sorted(by_cpu[cpu], key=lambda r: r["tick"])
        for i, r in enumerate(events):
            start = r["tick"]
            end = events[i + 1]["tick"] if i + 1 < len(events) else start + 1
            if end <= start:
                end = start + 1
            ax2.barh(row_idx, end - start, left=start, height=0.6,
                     color=tgid_color[r["tgid"]], edgecolor="black", linewidth=0.3)

    ax2.set_yticks(range(len(cpus)))
    ax2.set_yticklabels([f"cpu {c}" for c in cpus])
    ax2.set_xlabel("tick")
    ax2.set_title("CPU occupancy (color = tgid)")
    ax2.grid(alpha=0.3, axis="x")

    handles = [mpatches.Patch(color=tgid_color[tg], label=f"tgid {tg}") for tg in tgids]
    ax2.legend(handles=handles, loc="upper left", fontsize=8, ncol=len(tgids))

    if args.xlim:
        ax1.set_xlim(args.xlim[0], args.xlim[1])

    fig.tight_layout()
    fig.savefig(args.out, dpi=150)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()