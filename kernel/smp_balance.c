// smp_balance.c — Member 4: per-CPU run queues + load balancing (optional)
//
// Cut first if time-pressured — this is the stretch item.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"
#include "smp_balance.h"

void
smp_balance_check(void)
{
  // TODO(M4, optional): partition proc[] by p->cpu_affinity; migrate one
  // proc if load imbalance exceeds threshold.
}
