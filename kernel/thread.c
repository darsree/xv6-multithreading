// thread.c — Member 1: thread core & lifecycle
//
// Owns:
//   - tgid/tid assignment in allocproc()
//   - pagetable refcounting (pgrefcnt) on create/exit
//   - per-thread user stack allocation
//   - thread_create / thread_exit / thread_join
//   - group-wide teardown on exit()/kill()
//
// Do not put scheduler, sync, or stats logic in this file.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"
#include "thread.h"

int
thread_create(void (*fcn)(void*), void *arg, void *stack)
{
  // TODO(M1): clone trapframe setup pattern from fork() minus uvmcopy();
  // set epc, sp, a0. Increment pgrefcnt on the shared pagetable.
  panic("thread_create: not implemented");
}

void
thread_exit(void *retval)
{
  // TODO(M1): store retval, mark ZOMBIE, wake joiners on tgid channel,
  // decrement pgrefcnt; only call uvmfree() when refcount hits 0.
  panic("thread_exit: not implemented");
}

int
thread_join(int tid)
{
  // TODO(M1): sleep/wake on tgid channel, reap slot, return retval.
  return -1;
}

void
thread_group_teardown(int tgid)
{
  // TODO(M1): walk proc[] and terminate every proc sharing this tgid.
}
