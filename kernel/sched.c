// sched.c — Member 3: MLFQ + aging + adaptive quantum
//
// Owns: multi-level feedback queue logic, quantum expiry/demotion,
// aging/promotion, adaptive quantum via EMA of recent CPU usage.
// Independent of M1/M2's actual implementations — only needs the
// priority/queue_level/wait_ticks/quantum fields already in struct proc.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"
#include "sched.h"

void
mlfq_tick(struct proc *p)
{
  // TODO(M3): increment wait_ticks for READY-not-running procs; force
  // promote past AGING_THRESHOLD. Update adaptive quantum EMA for the
  // currently running proc.
}

struct proc *
sched_pick_next(void)
{
  // TODO(M3): scan queue_level 0..NQUEUES-1, return first RUNNABLE proc.
  return 0;
}
