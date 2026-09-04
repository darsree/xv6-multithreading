# Week 3 Integration Checklist

1. [ ] M1's thread.c API is frozen (no signature changes) — confirm.
2. [ ] M4 replaces the hand-rolled dummy mutex in donate.c with a real
       struct kmutex reference from sync.h.
3. [ ] M4 replaces the stub scheduler shape with M3's real queue
       struct from sched.h.
4. [ ] Run all four test programs (ttest_basic, synctest, schedtest,
       schedstat_dump + plot_timeline.py) back to back on the merged
       tree.
5. [ ] Merge `integration` branch into `main`; tag `week3-integration`.
