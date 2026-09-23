// pinv_test.c -- classic priority inversion, built so donation is VISIBLE.
//
// Run with ONE cpu so low and medium really compete:
//     make qemu CPUS=1
//   $ pinv_test
//
// Threads:
//   low    - takes the mutex first, then needs a FIXED AMOUNT OF CPU WORK
//            (not a fixed wall-clock time) while holding it.
//   medium - pure CPU hog, never touches the mutex.
//   high   - sleeps first (sleepers stay in the top queue), so by the time
//            it wakes and blocks on the mutex, low has already been demoted
//            below it. THAT is the situation donation is for.
//
// What you should see WITH donation:
//   donate: t=.. BOOST level 3 -> 0     (low lifted to high's level)
//   donate: t=.. RESTORE level .. -> 3  (after low unlocks)
//   "high waited" is short.
// With donate_boost() disabled, low round-robins with medium at the bottom
// queue and "high waited" is roughly twice as long or more.

#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define CHUNK        2000000   // one unit of busy work
#define LOW_TICKS    25        // low needs ~this many ticks of pure CPU
#define HIGH_SLEEP   15        // ticks high sleeps before it locks

int mid;                        // the contested mutex
int pmid;                       // serialises printing only
volatile int done;              // set by high, tells medium to stop
long low_chunks;                // how many CHUNKs low must do (calibrated)
int high_waited;                // filled in by high, printed by main

char low_stack[4096];
char medium_stack[4096];
char high_stack[4096];

void
say(char *who, char *what)
{
  mutex_lock(pmid);
  printf("[t=%d] [%s] %s\n", uptime(), who, what);
  mutex_unlock(pmid);
}

void
burn(long n)
{
  for (volatile long i = 0; i < n; i++) {}
}

// How many CHUNKs of busy work fit in one tick on THIS machine.
// Measured once, alone, before any thread exists, so the test behaves the
// same on a fast or slow host.
long
calibrate(void)
{
  int t = uptime();
  while (uptime() == t) {}          // start on a tick edge
  int start = uptime();
  long n = 0;
  while (uptime() - start < 4) { burn(CHUNK); n++; }
  return n / 4 ? n / 4 : 1;
}

void
low_worker(void *arg)
{
  say("low", "locking mutex");
  mutex_lock(mid);
  say("low", "got it, doing its critical section");
  for (long i = 0; i < low_chunks; i++)
    burn(CHUNK);
  say("low", "critical section done, unlocking");
  mutex_unlock(mid);
  thread_exit(0);
}

void
medium_worker(void *arg)
{
  int start = uptime();
  say("medium", "spinning, never touches the mutex");
  while (!done && uptime() - start < 400)
    burn(CHUNK);
  say("medium", "done");
  thread_exit(0);
}

void
high_worker(void *arg)
{
  pause(HIGH_SLEEP);               // stay at the top queue while low sinks
  say("high", "trying to lock -- should block on low");
  int t0 = uptime();
  mutex_lock(mid);
  high_waited = uptime() - t0;
  say("high", "GOT IT");
  mutex_unlock(mid);
  done = 1;
  thread_exit(0);
}

int
main(int argc, char *argv[])
{
  mid = mutex_create();
  pmid = mutex_create();
  if (mid < 0 || pmid < 0) {
    printf("pinv_test: mutex_create failed\n");
    exit(1);
  }

  low_chunks = calibrate() * LOW_TICKS;
  printf("pinv_test: low needs ~%d ticks of CPU inside the lock; "
         "high arrives after %d ticks\n", LOW_TICKS, HIGH_SLEEP);

  int lt = thread_create(low_worker, 0, low_stack + sizeof(low_stack) - 16);
  int mt = thread_create(medium_worker, 0, medium_stack + sizeof(medium_stack) - 16);
  int ht = thread_create(high_worker, 0, high_stack + sizeof(high_stack) - 16);

  thread_join(lt);
  thread_join(mt);
  thread_join(ht);

  printf("pinv_test: RESULT high waited %d ticks for the mutex\n", high_waited);
  exit(0);
}