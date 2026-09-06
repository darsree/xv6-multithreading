// sched.c — Member 3: MLFQ + aging + adaptive quantum
//
// Owns: multi-level feedback queue logic, quantum expiry/demotion,
// aging/promotion, adaptive quantum via EMA of recent CPU usage.
// Independent of M1/M2's actual implementations — only needs the
// priority/queue_level/wait_ticks/quantum fields already in struct proc
// (plus ticks_run/ema_pct, added alongside them for this file's own
// bookkeeping — nothing else in the tree reads or writes those two).
//
// Integration surface (exactly three call sites elsewhere, all one-liners):
//   allocproc()  (proc.c)  -> mlfq_init_proc(p)
//   scheduler()  (proc.c)  -> sched_pick_next()
//   sched()      (proc.c)  -> mlfq_on_switch_out(p)
//   usertrap()/kerneltrap() (trap.c) -> mlfq_tick(p) gates yield()
//   clockintr()  (trap.c)  -> mlfq_age_tick()

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"
#include "sched.h"

extern struct proc proc[NPROC];
extern uint ticks; // trap.c — used only to timestamp debug log lines below

// Base quantum (in timer ticks) per queue level. Level 0 is the
// top/most-interactive queue; level NQUEUES-1 is the most CPU-bound
// background queue. A proc's actual p->quantum starts here whenever it
// changes level, then gets nudged further by the adaptive-quantum EMA
// logic in mlfq_on_switch_out() below.
static const int base_quantum[NQUEUES] = {2, 4, 8, 16};

#define MLFQ_MIN_QUANTUM 1
#define MLFQ_MAX_QUANTUM 32

// EMA smoothing: new_ema = (old_ema*NUM + sample*(DEN-NUM)) / DEN.
// NUM/DEN = 7/10 -> 70% weight on history, 30% on the latest run.
#define EMA_ALPHA_NUM 7
#define EMA_ALPHA_DEN 10

// ema_pct thresholds that classify recent behavior.
#define CPU_BOUND_PCT 75 // used >=75% of quantum on average -> CPU-bound
#define IO_BOUND_PCT  35 // used <=35% of quantum on average -> I/O-bound

void
mlfq_init_proc(struct proc *p)
{
  p->queue_level = 0;
  p->wait_ticks = 0;
  p->ticks_run = 0;
  p->ema_pct = 0;
  p->quantum = base_quantum[0];
}

// NOTE on locking: p is always the proc currently RUNNING on THIS core
// when this is called (from usertrap()/kerneltrap()). ticks_run and
// quantum are only ever written by the core actually running p; the
// aging pass in mlfq_age_tick() only touches procs it finds RUNNABLE
// (never RUNNING), so there's no writer overlap and no lock is needed
// here — the same pattern xv6 already uses for other fields (e.g.
// p->trapframe) that are private to whichever core is running the proc.
int
mlfq_tick(struct proc *p)
{
  p->ticks_run++;
  return p->ticks_run >= p->quantum;
}

// Called once per global tick with tickslock already held (from
// clockintr(), matching the tickslock -> p->lock ordering wakeup()
// already uses from that same call site).
void
mlfq_age_tick(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == RUNNABLE) {
      p->wait_ticks++;
      if (p->wait_ticks > AGING_THRESHOLD) {
        if (p->queue_level != 0) {
          printk("mlfq: t=%d pid=%d tid=%d STARVED level %d->0 (aged out)\n",
                 ticks, p->pid, p->tid, p->queue_level);
          p->queue_level = 0;
          p->quantum = base_quantum[0];
        }
        p->wait_ticks = 0;
      }
    }
    release(&p->lock);
  }
}

// Called from sched(), p->lock held, immediately before the context
// switch away from p (i.e. exactly as p stops RUNNING). p->state tells
// us ZOMBIE (exiting; nothing to learn, just reset counters) vs
// everything else. We deliberately do NOT use RUNNABLE-vs-SLEEPING to
// decide CPU-bound-vs-I/O-bound: yield() is called both after trap.c
// sees mlfq_tick() report quantum expiry (genuinely CPU-bound) AND
// directly by other kernel code as a plain "give up the CPU" primitive
// (e.g. thread_group_teardown()'s busy-wait reap loop), so a RUNNABLE
// exit can also mean "yielded almost immediately, having used hardly
// any of its quantum." The one signal that's actually reliable
// regardless of caller is how many of its OWN allotted ticks it used:
// full use -> CPU-bound (demote); early exit for any reason -> treat
// like an I/O/cooperative-bound signal (promote).
void
mlfq_on_switch_out(struct proc *p)
{
  int used_pct;
  int old_level = p->queue_level;
  int fully_used;

  if (p->state == ZOMBIE) {
    p->ticks_run = 0;
    p->wait_ticks = 0;
    return;
  }

  used_pct = (p->quantum > 0) ? (p->ticks_run * 100) / p->quantum : 100;
  if (used_pct > 100)
    used_pct = 100;
  fully_used = p->ticks_run >= p->quantum;

  p->ema_pct = (p->ema_pct * EMA_ALPHA_NUM +
                used_pct * (EMA_ALPHA_DEN - EMA_ALPHA_NUM)) / EMA_ALPHA_DEN;

  // --- Quantum-expiry feedback: demote on full use. On early exit,
  // only promote if the proc actually did SOME work first (partial
  // quantum use before blocking) — a genuine I/O-bound signal. A
  // voluntary yield() with ~0 ticks used (e.g. a busy-wait loop like
  // thread_group_teardown()'s reap loop, which calls yield() directly,
  // not through the timer path) leaves the level unchanged instead of
  // being rewarded with promotion; otherwise a process that spins on
  // yield() with no real work could camp at level 0 forever and — via
  // sched_pick_next()'s per-level priority — starve everything below
  // it out indefinitely. ---
  if (fully_used) {
    if (p->queue_level < NQUEUES - 1)
      p->queue_level++;
  } else if (p->ticks_run > 0) {
    if (p->queue_level > 0)
      p->queue_level--;
  }
  if (p->queue_level != old_level) {
    printk("mlfq: t=%d pid=%d tid=%d level %d->%d ema=%d%% q=%d (%s)\n",
           ticks, p->pid, p->tid, old_level, p->queue_level, p->ema_pct,
           p->quantum, fully_used ? "quantum used up" : "blocked early");
    p->quantum = base_quantum[p->queue_level];
  }

  // --- Adaptive quantum: independently of any level change above,
  // nudge the quantum itself from the smoothed recent-behavior signal.
  // Shrink for CPU-bound procs (tighter slices -> fairer, more
  // responsive system); grow for I/O-bound ones (they rarely use the
  // full slice anyway, so a bigger one costs nothing but saves
  // context-switch overhead on the runs where they do use more). ---
  if (p->ema_pct >= CPU_BOUND_PCT) {
    if (p->quantum > MLFQ_MIN_QUANTUM)
      p->quantum--;
  } else if (p->ema_pct <= IO_BOUND_PCT) {
    int cap = base_quantum[p->queue_level] * 2;
    if (cap > MLFQ_MAX_QUANTUM)
      cap = MLFQ_MAX_QUANTUM;
    if (p->quantum < cap)
      p->quantum++;
  }

  p->wait_ticks = 0;
  p->ticks_run = 0;
}

// Per-level "resume scanning here next time" cursor, so procs at the
// same level get round-robin fairness instead of the scan always
// restarting at proc[0]. Without this, a low-table-index proc that
// keeps re-becoming RUNNABLE almost instantly (e.g. a busy-wait yield()
// loop) can win the scan every single time and permanently starve
// everything else — including, via strict per-level priority, every
// lower level too. Benign under a rare cross-core race (worst case:
// one call's rotation hint is briefly stale) — it's a fairness
// heuristic, not shared state anything depends on for correctness.
static int scan_cursor[NQUEUES];

// Scans queue_level 0..NQUEUES-1; within a level, scans proc[] starting
// from that level's rotating cursor (wrapping around), so procs at the
// same level get round-robin turns. Returns the chosen proc WITH ITS
// LOCK HELD, or 0 if nothing is RUNNABLE.
struct proc *
sched_pick_next(void)
{
  struct proc *p;
  int level, i, idx;

  for (level = 0; level < NQUEUES; level++) {
    for (i = 0; i < NPROC; i++) {
      idx = (scan_cursor[level] + i) % NPROC;
      p = &proc[idx];
      acquire(&p->lock);
      if (p->state == RUNNABLE && p->queue_level == level) {
        scan_cursor[level] = (idx + 1) % NPROC;
        return p; // caller dispatches, then releases p->lock.
      }
      release(&p->lock);
    }
  }
  return 0;
}