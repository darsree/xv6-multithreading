// ttest_basic.c — Member 1 deliverable
// Spawns N threads, each writes to a shared global, joins all, exits
// cleanly.
//
// Usage:
//   ttest_basic          run the basic create/write/join test
//   ttest_basic loop     spawn threads that loop forever, printing
//                        their pid — for manually testing
//                        `kill -9 <pid>` mid-run from another shell
//                        (confirms thread_group_teardown reaps the
//                        whole group, not just the killed thread).

#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define NTHREADS 4
#define ITERS    1000

int shared_counter = 0;   // written by every thread — proves shared memory
int per_thread[NTHREADS]; // one slot per thread, no data race by construction
int loop_forever = 0;

void
worker(void *arg)
{
  int id = (int)(uint64)arg;

  if (loop_forever) {
    for (;;) {
      per_thread[id]++;
      shared_counter++;
      pause(5);
    }
  }

  for (int i = 0; i < ITERS; i++) {
    per_thread[id]++;
    shared_counter++; // NOT synchronized on purpose: M2 hasn't landed
                       // mutexes yet. Demonstrates the shared address
                       // space works; a MISMATCH below just shows why
                       // a real mutex will be needed once M2 lands.
  }

  printf("ttest_basic: thread %d done, per_thread[%d]=%d\n", id, id,
         per_thread[id]);
  thread_exit((void *)0);
}

int
main(int argc, char *argv[])
{
  int tids[NTHREADS];

  if (argc > 1 && strcmp(argv[1], "loop") == 0) {
    loop_forever = 1;
    printf("ttest_basic: pid %d looping forever\n", getpid());
    printf("ttest_basic: run `kill -9 %d` from another shell to test "
           "group teardown\n", getpid());
  }

  for (int i = 0; i < NTHREADS; i++) {
    tids[i] = thread_create(worker, (void *)(uint64)i, 0);
    if (tids[i] < 0) {
      printf("ttest_basic: thread_create %d failed\n", i);
      exit(1);
    }
    printf("ttest_basic: spawned thread %d (tid %d)\n", i, tids[i]);
  }

  if (loop_forever) {
    // Never returns in practice — the point of this mode is to sit
    // here until something outside kills the whole group.
    thread_join(tids[0]);
    exit(0);
  }

  for (int i = 0; i < NTHREADS; i++) {
    if (thread_join(tids[i]) < 0) {
      printf("ttest_basic: thread_join %d (tid %d) failed\n", i, tids[i]);
      exit(1);
    }
  }

  int total = 0;
  for (int i = 0; i < NTHREADS; i++)
    total += per_thread[i];

  printf("ttest_basic: per-thread work: sum=%d expected=%d (%s)\n", total,
         NTHREADS * ITERS,
         total == NTHREADS * ITERS ? "OK" : "MISMATCH");
  printf("ttest_basic: shared_counter=%d expected=%d (%s%s)\n",
         shared_counter, NTHREADS * ITERS,
         shared_counter == NTHREADS * ITERS ? "OK" : "MISMATCH",
         shared_counter == NTHREADS * ITERS
             ? ""
             : " -- unsynchronized increment, expected until M2's mutex lands");

  printf("ttest_basic: all threads joined, exiting\n");
  exit(0);
}
