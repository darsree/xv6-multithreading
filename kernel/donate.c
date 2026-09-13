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
extern uint ticks; // trap.c — timestamp donation events so they can be
                   // correlated with the schedstat timeline (mlfq's
                   // printk lines already do this; donation's didn't).

// Call once per proc, from allocproc(), alongside mlfq_init_proc().
void
donate_init_proc(struct proc *p)
{
  p->donated_priority = -1; // -1 = not currently boosted
  p->nlocks_held = 0;
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
      // BUG FIX: this used to save donated_priority unconditionally,
      // before checking whether a boost was actually warranted. That
      // meant ANY contended mutex -- including an ordinary, harmless
      // one like a print-serializing lock shared by same-priority
      // threads -- got marked as "donation active," even though
      // queue_level was never touched. donate_restore() would then
      // fire later and print a meaningless "RESTORE 3 -> 3" (no
      // actual change) on completely uneventful mutex use, burying
      // the real donation events in noise. Only save/boost when the
      // blocker is genuinely more urgent.
      if (blocker_level < p->queue_level) {
        if (p->donated_priority == -1)
          p->donated_priority = p->queue_level; // save original, first boost only
        printk("donate: t=%d pid=%d BOOST level %d -> %d (blocker wants %d)\n",
               ticks, p->pid, p->queue_level, blocker_level, blocker_level);
        p->queue_level = blocker_level;
      }
      release(&p->lock);
      return;
    }
    release(&p->lock);
  }
}

// Restore the (former) owner's queue_level to what it was before any
// donation.
//
// FLAW FIX: this used to be called unconditionally from every
// kmutex_unlock(), which is wrong whenever the owner holds MORE THAN
// ONE kmutex at a time. donated_priority only remembers the level
// from the FIRST boost (donate_boost() only sets it when it's -1), so
// restoring on the first unlock wipes that bookkeeping even if a
// second, still-held lock has its own waiter relying on the boost —
// the owner's priority prematurely drops back down, and priority
// inversion protection lapses until the next kmutex_lock() spin
// iteration re-donates.
//
// The caller (kmutex_unlock() in sync.c) now only invokes this once
// the owner's p->nlocks_held has dropped to 0, i.e. once it no longer
// holds ANY kmutex — so a boost granted for lock A can't be erased by
// releasing unrelated lock B while A (or C, D...) is still held.
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
        printk("donate: t=%d pid=%d RESTORE level %d -> %d\n",
               ticks, p->pid, p->queue_level, p->donated_priority);
        p->queue_level = p->donated_priority;
        p->donated_priority = -1;
      }
      release(&p->lock);
      return;
    }
    release(&p->lock);
  }
}