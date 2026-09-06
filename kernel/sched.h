// sched.h — Member 3: MLFQ scheduler
#ifndef SCHED_H
#define SCHED_H

#define NQUEUES 4
#define AGING_THRESHOLD 100  // ticks a proc can wait before force-promote

// Called from allocproc() so every new/recycled proc slot starts at the
// top queue with fresh aging/quantum state instead of inheriting
// whatever a previous occupant left behind.
void mlfq_init_proc(struct proc *p);

// Called once per timer tick from trap.c for bookkeeping (aging, quantum
// expiry). Keep this the ONLY per-tick hook trap.c needs to call.
// Returns 1 if the running proc's quantum has just expired (caller
// should yield()), 0 otherwise.
int mlfq_tick(struct proc *p);

// Called once per GLOBAL tick (cpuid()==0) from clockintr(), with
// tickslock held. Ages every RUNNABLE-but-not-running proc and force-
// promotes anything that has waited past AGING_THRESHOLD, so a
// low-priority proc can never starve forever behind busier ones.
void mlfq_age_tick(void);

// Called from sched() (proc.c), p->lock already held, right before the
// context switch away from p. p->state reflects why p is leaving
// RUNNING (RUNNABLE = quantum expired, SLEEPING = blocked early, ZOMBIE
// = exiting), which is exactly the signal MLFQ feedback and the
// adaptive-quantum EMA need.
void mlfq_on_switch_out(struct proc *p);

// Called from proc.c's scheduler() loop to pick the next proc to run.
// Scans queue_level 0..NQUEUES-1 (0 = highest priority) and returns the
// first RUNNABLE proc found, WITH ITS LOCK HELD (same convention as
// allocproc()) — or 0 if nothing is runnable. Keeps the MLFQ scan logic
// out of proc.c.
struct proc *sched_pick_next(void);

#endif // SCHED_H
