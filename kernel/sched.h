// sched.h — Member 3: MLFQ scheduler
#ifndef SCHED_H
#define SCHED_H


#define NQUEUES 4
#define AGING_THRESHOLD 100  // ticks a proc can wait before force-promote

// Called once per timer tick from trap.c for bookkeeping (aging, quantum
// expiry). Keep this the ONLY hook trap.c needs to call.
void mlfq_tick(struct proc *p);

// Called from proc.c's scheduler() loop to pick the next proc to run.
// Keeps the ~50 lines of MLFQ logic out of proc.c.
struct proc *sched_pick_next(void);

#endif // SCHED_H
