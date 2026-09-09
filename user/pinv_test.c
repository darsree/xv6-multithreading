// pinvtest.c — the CLASSIC priority inversion scenario, not just a
// two-thread donation check.
//
// Three threads:
//   low    - grabs the mutex almost immediately, then does a long
//            "critical section" of work while holding it.
//   medium - a pure CPU hog that never touches the mutex at all. It
//            doesn't care about the critical section -- it's just
//            greedy for CPU, the whole run.
//   high   - waits a moment (so low gets the mutex first), then tries
//            to lock the SAME mutex and blocks.
//
// The problem this demonstrates: high is blocked on low, so high's
// progress depends entirely on low finishing and unlocking. But if
// low has no priority boost, MLFQ just sees "two CPU-hungry threads"
// (low and medium) and shares the CPU between them -- so medium,
// which has NOTHING to do with the critical section, ends up
// indirectly delaying high. That's priority inversion: a
// low-importance thread (medium) blocks a high-importance one (high)
// through an intermediary (low).
//
// Your donate_boost()/donate_restore() calls (kernel/donate.c) exist
// specifically to prevent this: the moment high blocks on low's
// mutex, low should get boosted toward high's queue level so it can
// finish and get out of the way, instead of getting stuck
// round-robining with medium.
//
// What to look for in the chart: low's queue_level should visibly
// dip back toward 0 right around when high starts blocking, then
// jump back up (donate_restore) right after unlock -- while medium's
// level just climbs steadily the whole time, undisturbed.
//
// Usage (inside the xv6 shell):
//   $ pinvtest
//   $ schedstat_dump pinv.csv
// then: python3 tools/viz/plot_timeline.py pinv.csv -o pinv.png

#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define DURATION 150   // ticks; medium hog runs roughly this long

int mid;   // the contested mutex
int pmid;  // print-serializing mutex, output clarity only

char low_stack[4096];
char medium_stack[4096];
char high_stack[4096];

void
safe_print(char *msg)
{
  mutex_lock(pmid);
  printf("%s", msg);
  mutex_unlock(pmid);
}

void
burn(long n)
{
  for (volatile long i = 0; i < n; i++) {}
}

void
low_worker(void *arg)
{
  safe_print("[low] locking mutex immediately...\n");
  mutex_lock(mid);
  safe_print("[low] got it -- now doing a long 'critical section'\n");
  burn(150000000);   // long enough that, without donation, MLFQ would
                      // demote low and let medium eat its turns
  safe_print("[low] critical section done, unlocking\n");
  mutex_unlock(mid);
  thread_exit(0);
}

// Never touches the mutex. Pure CPU greed, the whole run -- the
// "innocent bystander" that shouldn't be able to delay high, but can,
// without donation.
void
medium_worker(void *arg)
{
  int start = uptime();
  safe_print("[medium] spinning, doesn't care about the mutex at all\n");
  while (uptime() - start < DURATION)
    burn(2000000);
  safe_print("[medium] done\n");
  thread_exit(0);
}

void
high_worker(void *arg)
{
  burn(20000000);   // small head start for low, so low grabs the
                      // mutex first, deterministically
  safe_print("[high] trying to lock -- should block on low\n");
  mutex_lock(mid);
  safe_print("[high] GOT IT -- this is the moment that matters\n");
  mutex_unlock(mid);
  thread_exit(0);
}

int
main(int argc, char *argv[])
{
  int low_tid, medium_tid, high_tid;

  mid = mutex_create();
  pmid = mutex_create();
  if (mid < 0 || pmid < 0) {
    printf("pinvtest: mutex_create failed\n");
    exit(1);
  }

  printf("pinvtest: low grabs mutex, medium hogs CPU, "
         "high blocks on low -- watch for donation kicking in\n");

  low_tid = thread_create(low_worker, 0,
                           low_stack + sizeof(low_stack) - 16);
  medium_tid = thread_create(medium_worker, 0,
                              medium_stack + sizeof(medium_stack) - 16);
  high_tid = thread_create(high_worker, 0,
                            high_stack + sizeof(high_stack) - 16);

  thread_join(low_tid);
  thread_join(medium_tid);
  thread_join(high_tid);

  printf("pinvtest: done. Now run: schedstat_dump pinv.csv\n");
  exit(0);
}