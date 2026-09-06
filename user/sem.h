// sem.h — Member 2: counting semaphore, built entirely in user space
// as a thin wrapper over kmutex + cv. No new syscalls, no kernel
// state — the semaphore's own state (count) lives in ordinary shared
// memory, visible to every thread the same way any other global is.
#ifndef SEM_H
#define SEM_H

#include "kernel/types.h"
#include "user/user.h"

struct sem {
  int mutex;
  int cv;
  int count;
};

static inline void
sem_init(struct sem *s, int initial_count)
{
  s->mutex = mutex_create();
  s->cv = cv_create();
  s->count = initial_count;
}

static inline void
sem_wait(struct sem *s)
{
  mutex_lock(s->mutex);
  while (s->count == 0)
    cv_wait(s->cv, s->mutex);
  s->count--;
  mutex_unlock(s->mutex);
}

static inline void
sem_post(struct sem *s)
{
  mutex_lock(s->mutex);
  s->count++;
  cv_signal(s->cv);
  mutex_unlock(s->mutex);
}

#endif // SEM_H