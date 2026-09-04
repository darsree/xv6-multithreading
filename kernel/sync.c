// sync.c — Member 2: synchronization primitives
//
// Owns: kmutex array + cv array, syscall bodies for mutex/cv/(sem).
// Talks to sleep()/wakeup() already in xv6 core — does not depend on
// Member 1's thread.c being finished.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"
#include "sync.h"

static struct kmutex mutexes[NKMUTEX] __attribute__((unused));
static struct cv cvs[NKCV] __attribute__((unused));

void
kmutex_init(void)
{
  // TODO(M2): initspin each mutex lock, mark all slots invalid/free.
}

int
kmutex_create(void)
{
  // TODO(M2): find a free slot, mark valid, return index (mirrors fd table).
  return -1;
}

int
kmutex_lock(int id)
{
  // TODO(M2): acquire spinlock, sleep while locked, set locked=1, owner=self.
  return -1;
}

int
kmutex_unlock(int id)
{
  // TODO(M2): set locked=0, owner=-1, wakeup waiters.
  return -1;
}

int
kmutex_destroy(int id)
{
  return -1;
}

int
cv_wait(int cv_id, int mutex_id)
{
  // TODO(M2): release mutex, sleep on &cvs[cv_id], reacquire mutex on wake.
  return -1;
}

int
cv_signal(int cv_id)
{
  // TODO(M2): wakeup one waiter on &cvs[cv_id].
  return -1;
}

int
cv_broadcast(int cv_id)
{
  // TODO(M2): wakeup all waiters on &cvs[cv_id].
  return -1;
}

int
sem_create(int count)
{
  // Optional wrapper over kmutex + cv.
  return -1;
}

int
sem_wait(int id)
{
  return -1;
}

int
sem_post(int id)
{
  return -1;
}
