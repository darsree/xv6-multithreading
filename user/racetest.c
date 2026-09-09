// racetest.c — proves threads share one address space by deliberately
// racing on a shared global, then fixing it with a mutex.
//
// Part 1: NTHREADS threads each increment the SAME global variable
// ITERS times, with no locking. If threads really share memory and
// really run concurrently, some increments get lost (thread A reads
// the value, thread B reads the same old value before A writes back,
// B's increment overwrites A's). Final count comes out LESS than
// NTHREADS * ITERS.
//
// Part 2: identical increment loop, but guarded by a kmutex. Final
// count matches NTHREADS * ITERS exactly, every time.
//
// This is the clearest possible demonstration that (a) your threads
// truly share memory (a process fork() would give each child its own
// private copy, and there'd be nothing to race on), (b) they truly
// run concurrently (a race requires real overlap, not just
// interleaved single-core scheduling of independent memory), and (c)
// your mutex actually provides mutual exclusion.
//
// Usage (inside the xv6 shell):
//   $ racetest

#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define NTHREADS 4
#define ITERS    20000

volatile int unsafe_counter = 0;  // volatile: forces a real read+write
                                   // every iteration instead of the
                                   // compiler caching it in a register
int safe_counter = 0;
int cmid;

char t_stack[NTHREADS][4096];

void
unsafe_worker(void *arg)
{
  for (int i = 0; i < ITERS; i++) {
    int tmp = unsafe_counter;   // read
    tmp = tmp + 1;              // modify
    unsafe_counter = tmp;       // write -- another thread can sneak
                                 // in between this read and this write
  }
  thread_exit(0);
}

void
safe_worker(void *arg)
{
  for (int i = 0; i < ITERS; i++) {
    mutex_lock(cmid);
    safe_counter++;
    mutex_unlock(cmid);
  }
  thread_exit(0);
}

int
main(int argc, char *argv[])
{
  int tid[NTHREADS];
  int i;
  int expected = NTHREADS * ITERS;

  printf("racetest: %d threads x %d increments each, expecting %d\n",
         NTHREADS, ITERS, expected);

  printf("\n=== WITHOUT a mutex ===\n");
  for (i = 0; i < NTHREADS; i++)
    tid[i] = thread_create(unsafe_worker, 0,
                            t_stack[i] + sizeof(t_stack[i]) - 16);
  for (i = 0; i < NTHREADS; i++)
    thread_join(tid[i]);

  printf("expected: %d\n", expected);
  printf("actual:   %d\n", unsafe_counter);
  if (unsafe_counter != expected)
    printf("-> LOST %d increments to a race condition "
           "(proves real shared memory + real concurrency)\n",
           expected - unsafe_counter);
  else
    printf("-> no lost updates this run -- try again or raise ITERS, "
           "timing-dependent\n");

  cmid = mutex_create();
  printf("\n=== WITH a mutex, same increment pattern ===\n");
  for (i = 0; i < NTHREADS; i++)
    tid[i] = thread_create(safe_worker, 0,
                            t_stack[i] + sizeof(t_stack[i]) - 16);
  for (i = 0; i < NTHREADS; i++)
    thread_join(tid[i]);

  printf("expected: %d\n", expected);
  printf("actual:   %d\n", safe_counter);
  printf("-> %s\n",
         safe_counter == expected
             ? "PASS: exact match, mutex prevented every lost update"
             : "FAIL: mismatch even with mutex -- check kmutex impl");

  exit(0);
}