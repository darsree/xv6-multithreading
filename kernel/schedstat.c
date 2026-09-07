// schedstat.c — Member 4: scheduler instrumentation (getschedstat)
//
// Ring buffer logging (tick, tgid, tid, is_thread, state, cpu_id,
// priority) on every context switch. Self-contained: only needs a
// single hook call inserted near swtch() in proc.c.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"
#include "schedstat.h"

static struct schedstat_rec ring[SCHEDSTAT_RINGSIZE];
static int ring_head = 0;
static int ring_count = 0;      // how many valid records so far (caps at RINGSIZE)
static struct spinlock schedstat_lock;

void
schedstatinit(void)
{
  initlock(&schedstat_lock, "schedstat");
}

void
record_schedstat(struct proc *p, int cpu_id)
{
  extern uint ticks;
  struct schedstat_rec *r;

  acquire(&schedstat_lock);
  r = &ring[ring_head];
  r->tick      = ticks;
  r->tgid      = p->tgid;
  r->tid       = p->tid;
  r->is_thread = p->is_thread;
  r->state     = p->state;
  r->cpu_id    = cpu_id;
   r->priority  = p->queue_level;   // what donation actually modifies
  ring_head = (ring_head + 1) % SCHEDSTAT_RINGSIZE;
  if (ring_count < SCHEDSTAT_RINGSIZE)
    ring_count++;
  release(&schedstat_lock);
}

int
getschedstat(void *buf, int max)
{
  struct proc *p = myproc();
  static struct schedstat_rec tmp[SCHEDSTAT_RINGSIZE];
  int n, i, start;

  acquire(&schedstat_lock);
  n = ring_count;
  if (n > max)
    n = max;
  start = (ring_count < SCHEDSTAT_RINGSIZE) ? 0 : ring_head;
  for (i = 0; i < n; i++)
    tmp[i] = ring[(start + i) % SCHEDSTAT_RINGSIZE];
  release(&schedstat_lock);

    if (copyout(p->pagetable, p->sz, (uint64)buf, (char *)tmp,
              n * sizeof(struct schedstat_rec)) < 0)
    return -1;

  return n;
}