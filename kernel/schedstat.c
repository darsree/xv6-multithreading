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

static struct schedstat_rec ring[SCHEDSTAT_RINGSIZE] __attribute__((unused));
static int ring_head __attribute__((unused)) = 0;

void
record_schedstat(struct proc *p, int cpu_id)
{
  // TODO(M4): fill ring[ring_head] from p's fields + cpu_id, advance head.
}

int
getschedstat(void *buf, int max)
{
  // TODO(M4): copyout up to `max` records from ring buffer to user buf.
  return -1;
}
