// donate.c — Member 4: priority donation
//
// Depends conceptually on M2's kmutex.owner field (already in the
// skeleton) but should be built/tested against your own stub mutex
// until Week 3 integration.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"
#include "donate.h"

void
donate_boost(int owner_pid, int blocker_priority)
{
  // TODO(M4): find proc by pid, save donated_priority, boost effective priority.
}

void
donate_restore(int pid)
{
  // TODO(M4): restore proc's original priority on mutex unlock.
}
