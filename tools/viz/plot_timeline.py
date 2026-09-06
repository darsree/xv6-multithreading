#!/usr/bin/env python3
"""plot_timeline.py — Member 4

Reads schedstat CSV (from user/schedstat_dump.c output, captured via
QEMU console redirect) and renders a Gantt-style timeline colored by
tgid (process-vs-thread view) and queue level (scheduler-behavior view).

Usage:
    python3 plot_timeline.py schedstat.csv
"""
import sys

def main():
    if len(sys.argv) < 2:
        print("usage: plot_timeline.py <schedstat.csv>")
        sys.exit(1)
    # TODO(M4): pandas.read_csv + matplotlib broken_barh Gantt chart.
    print("plot_timeline: TODO implement Gantt chart rendering")

if __name__ == "__main__":
    main()
