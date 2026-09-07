// donate.c — Member 4: priority donation
//
// Fixes priority inversion: when a thread blocks on a mutex held by
// a proc in a less-urgent (higher-numbered) MLFQ queue, temporarily
// move the holder into the blocker's queue level so the scheduler
// (which picks strictly by queue_level, level 0 first — see M3's
// sched_pick_next()) actually runs it soon, instead of an unrelated
// mid-priority proc running in between.
//
// NOTE: p->priority in proc.h is declared but never read/written
// anywhere else in this codebase — only queue_level drives the real
// scheduler. So donation operates directly on queue_level. The
// donated_priority field is repurposed here purely as "the owner's
// saved original queue_level, so we can restore it later" — it holds
// -1 when no donation is currently active on that proc.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"
#include "donate.h"

extern struct proc proc[NPROC];

// Call once per proc, from allocproc(), alongside mlfq_init_proc().
void
donate_init_proc(struct proc *p)
{
  p->donated_priority = -1; // -1 = not currently boosted
}

// owner_pid: pid of the mutex holder (from m->owner).
// blocker_level: the blocking thread's OWN queue_level (its urgency —
// lower is more urgent). If that's more urgent than the owner's
// current level, boost the owner into it.
void
donate_boost(int owner_pid, int blocker_level)
{
  struct proc *p;

  if (owner_pid < 0)
    return;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == owner_pid) {
      if (p->donated_priority == -1)
        p->donated_priority = p->queue_level; // save original, first boost only
      if (blocker_level < p->queue_level) {
        printk("donate: pid=%d BOOST level %d -> %d (blocker wants %d)\n",
               p->pid, p->queue_level, blocker_level, blocker_level);
        p->queue_level = blocker_level;
      }
      release(&p->lock);
      return;
    }
    release(&p->lock);
  }
}

// Restore the (former) owner's queue_level to what it was before any
// donation. Simple version — single active donation at a time, which
// matches kmutex_lock's one-owner-at-a-time model.
void
donate_restore(int pid)
{
  struct proc *p;

  if (pid < 0)
    return;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid) {
      if (p->donated_priority != -1) {
        printk("donate: pid=%d RESTORE level %d -> %d\n",
               p->pid, p->queue_level, p->donated_priority);
        p->queue_level = p->donated_priority;
        p->donated_priority = -1;
      }
      release(&p->lock);
      return;
    }
    release(&p->lock);
  }
}
