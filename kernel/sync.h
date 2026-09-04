// sync.h — Member 2: synchronization primitives
#ifndef SYNC_H
#define SYNC_H


#define NKMUTEX 64
#define NKCV    64

struct kmutex {
  struct spinlock lk;
  int locked;
  int owner;   // pid/tid of holder, -1 if unlocked (used by M4's donation)
  int valid;
};

struct cv {
  int valid;
  // wait channel is simply &cv[i]; no extra state needed for basic impl
};

void kmutex_init(void);
int  kmutex_create(void);
int  kmutex_lock(int id);
int  kmutex_unlock(int id);
int  kmutex_destroy(int id);

int  cv_wait(int cv_id, int mutex_id);
int  cv_signal(int cv_id);
int  cv_broadcast(int cv_id);

// Optional: semaphore as thin wrapper over mutex + cv
int  sem_create(int count);
int  sem_wait(int id);
int  sem_post(int id);

#endif // SYNC_H
