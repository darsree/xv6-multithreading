#include "kernel/types.h"
#include "user/user.h"
#include "user/sem.h"

#define NITEMS 20
int buf[1];
int count = 0;
int mid, full_cv, empty_cv;

void
producer(void *arg)
{
  for (int i = 0; i < NITEMS; i++) {
    mutex_lock(mid);
    while (count == 1)
      cv_wait(full_cv, mid);
    buf[0] = i;
    count = 1;
    printf("produced %d\n", i);
    cv_signal(empty_cv);
    mutex_unlock(mid);
  }
  thread_exit(0);
}

void
consumer(void *arg)
{
  for (int i = 0; i < NITEMS; i++) {
    mutex_lock(mid);
    while (count == 0)
      cv_wait(empty_cv, mid);
    int v = buf[0];
    count = 0;
    printf("consumed %d\n", v);
    cv_signal(full_cv);
    mutex_unlock(mid);
  }
  thread_exit(0);
}

int sig_mtx, sig_cv, woken;

void
waiter(void *arg)
{
  int id = (int)(uint64)arg;
  mutex_lock(sig_mtx);
  printf("waiter %d: blocking\n", id);
  cv_wait(sig_cv, sig_mtx);
  woken++;
  printf("waiter %d: woke (woken=%d)\n", id, woken);
  mutex_unlock(sig_mtx);
  thread_exit(0);
}

void
test_signal_vs_broadcast(void)
{
  sig_mtx = mutex_create();
  sig_cv = cv_create();
  woken = 0;

  int t1 = thread_create(waiter, (void *)1, 0);
  int t2 = thread_create(waiter, (void *)2, 0);
  int t3 = thread_create(waiter, (void *)3, 0);

  pause(20);

  mutex_lock(sig_mtx);
  cv_signal(sig_cv);
  mutex_unlock(sig_mtx);
  pause(20);
  printf("after cv_signal: woken=%d (expect 1)\n", woken);

  mutex_lock(sig_mtx);
  cv_broadcast(sig_cv);
  mutex_unlock(sig_mtx);
  pause(20);
  printf("after cv_broadcast: woken=%d (expect 3)\n", woken);

  thread_join(t1);
  thread_join(t2);
  thread_join(t3);
}

#define NTHREADS 4
#define ITERS 2000
int shared_counter;
int stress_mtx;

void
incrementer(void *arg)
{
  for (int i = 0; i < ITERS; i++) {
    mutex_lock(stress_mtx);
    shared_counter++;
    mutex_unlock(stress_mtx);
  }
  thread_exit(0);
}

void
test_mutex_stress(void)
{
  stress_mtx = mutex_create();
  shared_counter = 0;
  int tids[NTHREADS];
  for (int i = 0; i < NTHREADS; i++)
    tids[i] = thread_create(incrementer, 0, 0);
  for (int i = 0; i < NTHREADS; i++)
    thread_join(tids[i]);
  printf("counter=%d expected=%d %s\n", shared_counter, NTHREADS * ITERS,
         shared_counter == NTHREADS * ITERS ? "OK" : "MISMATCH");
}

void
test_errors(void)
{
  printf("mutex_lock(-1)=%d (expect -1)\n", mutex_lock(-1));
  printf("mutex_unlock(9999)=%d (expect -1)\n", mutex_unlock(9999));

  int id = mutex_create();
  printf("unlock-without-holding=%d (expect -1)\n", mutex_unlock(id));
  mutex_lock(id);
  mutex_unlock(id);
  printf("double-unlock=%d (expect -1)\n", mutex_unlock(id));

int ids[100];
  int last = -2;
  for (int i = 0; i < 100; i++)
    ids[i] = last = mutex_create();
  printf("mutex_create exhaustion last=%d (expect -1)\n", last);
  for (int i = 0; i < 100; i++)
    if (ids[i] >= 0)
      mutex_destroy(ids[i]);
}

// --- test 5: does a thread stuck in kmutex_lock actually unblock on kill? ---
int block_mtx;

void
blocker(void *arg)
{
  int r = mutex_lock(block_mtx);
  if (r == 0) {
    printf("blocker: BUG — actually acquired the lock\n");
    mutex_unlock(block_mtx);
  } else {
    printf("blocker: mutex_lock returned %d after kill (correct)\n", r);
  }
  thread_exit(0);
}

void
test_killed(void)
{
  block_mtx = mutex_create();
  mutex_lock(block_mtx); // main holds it forever, on purpose

  int t = thread_create(blocker, 0, 0);
  pause(10); // let blocker actually reach the blocking point first
  printf("READY pid=%d -- now run: kill %d\n", getpid(), getpid());

  thread_join(t);
  printf("test_killed: thread_join returned cleanly, blocker reaped\n");
  exit(0);
}


struct sem sem_slots;

void
sem_producer(void *arg)
{
  for (int i = 0; i < NITEMS; i++) {
    sem_wait(&sem_slots);      // block if no capacity
    printf("sem: produced %d\n", i);
  }
  thread_exit(0);
}

void
sem_consumer(void *arg)
{
  for (int i = 0; i < NITEMS; i++) {
    pause(2);
    printf("sem: consumed, freeing a slot\n");
    sem_post(&sem_slots);
  }
  thread_exit(0);
}

void
test_semaphore(void)
{
  sem_init(&sem_slots, 3); // capacity 3, so producer can run ahead by 3 before blocking

  int t1 = thread_create(sem_producer, 0, 0);
  int t2 = thread_create(sem_consumer, 0, 0);
  thread_join(t1);
  thread_join(t2);
  printf("synctest: sem test done\n");
}

int
main(int argc, char *argv[])
{
  if (argc > 1 && strcmp(argv[1], "signal") == 0) {
    test_signal_vs_broadcast();
  } else if (argc > 1 && strcmp(argv[1], "stress") == 0) {
    test_mutex_stress();
  } else if (argc > 1 && strcmp(argv[1], "errors") == 0) {
    test_errors();
  } else if (argc > 1 && strcmp(argv[1], "killed") == 0) {
    test_killed();
  } else if (argc > 1 && strcmp(argv[1], "sem") == 0) {
    test_semaphore();
  } else {
    mid = mutex_create();
    full_cv = cv_create();
    empty_cv = cv_create();
    int t1 = thread_create(producer, 0, 0);
    int t2 = thread_create(consumer, 0, 0);
    thread_join(t1);
    thread_join(t2);
    printf("synctest: PASS\n");
  }
  exit(0);
}