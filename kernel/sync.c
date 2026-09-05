// sync.c — Member 2: synchronization primitives
#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"
#include "sync.h"

extern struct proc proc[NPROC];

static struct kmutex mutexes[NKMUTEX];
static struct cv cvs[NKCV];
static struct spinlock cv_lock;

void
kmutex_init(void)
{
  initlock(&cv_lock, "cv");
  for (int i = 0; i < NKMUTEX; i++) {
    initlock(&mutexes[i].lk, "kmutex");
    mutexes[i].locked = 0;
    mutexes[i].owner = -1;
    mutexes[i].valid = 0;
  }
  for (int i = 0; i < NKCV; i++)
    cvs[i].valid = 0;
}

int
kmutex_create(void)
{
  for (int i = 0; i < NKMUTEX; i++) {
    struct kmutex *m = &mutexes[i];
    acquire(&m->lk);
    if (!m->valid) {
      m->valid = 1;
      m->locked = 0;
      m->owner = -1;
      release(&m->lk);
      return i;
    }
    release(&m->lk);
  }
  return -1;
}

static struct spinlock cv_lock;
// in kmutex_init(): initlock(&cv_lock, "cv");

int
cv_create(void)
{
  acquire(&cv_lock);
  for (int i = 0; i < NKCV; i++) {
    if (!cvs[i].valid) {
      cvs[i].valid = 1;
      release(&cv_lock);
      return i;
    }
  }
  release(&cv_lock);
  return -1;
}

int
kmutex_lock(int id)
{
  if (id < 0 || id >= NKMUTEX)
    return -1;
  struct kmutex *m = &mutexes[id];

  acquire(&m->lk);
  if (!m->valid) {
    release(&m->lk);
    return -1;
  }
  while (m->locked) {
    if (killed(myproc())) {
      release(&m->lk);
      return -1;
    }
    sleep_prepare(m);
    release(&m->lk);
    sleep();
    acquire(&m->lk);
  }
  m->locked = 1;
  m->owner = myproc()->pid;
  release(&m->lk);
  return 0;
}


int
kmutex_unlock(int id)
{
  if (id < 0 || id >= NKMUTEX)
    return -1;
  struct kmutex *m = &mutexes[id];

  acquire(&m->lk);
  if (!m->valid || !m->locked) {
    release(&m->lk);
    return -1;
  }
  m->locked = 0;
  m->owner = -1;
  release(&m->lk);
  wakeup(m); // thundering herd is fine: exactly one re-acquires, rest re-sleep
  return 0;
}

int
kmutex_destroy(int id)
{
  if (id < 0 || id >= NKMUTEX)
    return -1;
  struct kmutex *m = &mutexes[id];
  acquire(&m->lk);
  if (!m->valid || m->locked) { // refuse to destroy a held lock
    release(&m->lk);
    return -1;
  }
  m->valid = 0;
  release(&m->lk);
  return 0;
}

// Wake exactly one sleeper on chan. Plain wakeup() (proc.c) always
// wakes *every* proc sleeping on chan — there's no "wake one" in this
// xv6, so cv_signal needs its own scan that stops at the first match.
static void
wakeup_one(void *chan)
{
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->chan == chan) {
      p->chan = 0;
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&p->lock);
      return;
    }
    release(&p->lock);
  }
}

int
cv_wait(int cv_id, int mutex_id)
{
  if (cv_id < 0 || cv_id >= NKCV || mutex_id < 0 || mutex_id >= NKMUTEX)
    return -1;
  struct cv *c = &cvs[cv_id];
  if (!c->valid)
    return -1;

  sleep_prepare(c);
  if (kmutex_unlock(mutex_id) < 0)
    return -1;

  sleep();

  if (killed(myproc()))
    return -1;   // don't try to re-acquire on the way out if we've been killed

  return kmutex_lock(mutex_id);
}

int
cv_signal(int cv_id)
{
  if (cv_id < 0 || cv_id >= NKCV || !cvs[cv_id].valid)
    return -1;
  wakeup_one(&cvs[cv_id]);
  return 0;
}

int
cv_broadcast(int cv_id)
{
  if (cv_id < 0 || cv_id >= NKCV || !cvs[cv_id].valid)
    return -1;
  wakeup(&cvs[cv_id]);
  return 0;
}

