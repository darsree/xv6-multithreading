// don_super.c — "super dooper" demo scenario for the professor.
//
// Same idea as donatetest.c (Member 4), but with enough threads and
// mixed behavior to make every panel of tools/viz/plot_timeline.py
// show something interesting in a single run:
//
//   - 2 CPU-bound hog threads      -> demote and settle at the bottom
//                                      MLFQ queue (big steps down in
//                                      the priority panel)
//   - 2 I/O-bound threads          -> block/wake repeatedly, stay near
//                                      the top queue the whole time
//   - 1 "starved" low-effort thread-> barely runs, waits behind the
//                                      hogs, eventually gets force-
//                                      promoted by aging (a jump back
//                                      UP in the priority panel)
//   - 1 low-priority mutex holder
//     + 1 high-priority blocker    -> classic priority-donation boost
//                                      + restore (the red arrows)
//
// All threads share one process (one tgid), so the Gantt panel shows
// one color with lots of tid-level movement across CPUs — good for
// demonstrating "threads, not processes" to a professor who's used to
// seeing xv6 demos as separate forked processes.
//
// TIMING FIX (see NOTE below): every phase is now driven off uptime()
// tick counts instead of raw burn-loop iteration counts, so the demo
// is deterministic across hosts of different speed. With the old
// fixed-iteration burns, on a fast host `low`'s 50M-iteration warmup
// and `high`'s 60M-iteration warmup finished only ~1 tick apart, so
// `low` frequently released the mutex before `high` ever called
// mutex_lock() -- no contention window, no donation to observe, and
// not nearly enough continuous running time for `low` or `starved`
// to actually get demoted. Driving everything off ticks fixes that
// regardless of how fast the underlying machine is.
//
// Usage (inside the xv6 shell):
//   $ don_super
//   $ schedstat_dump sched.csv
// then pull sched.csv out (see tools/viz/plot_timeline.py's docstring)
// and run the plotting script on your host.

#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"
#include "user/schedstat.h"

#define NHOGS     2
#define NIO       2
#define DURATION  300   // ticks; clockintr fires ~10x/sec, so ~30s total.
                         // Bumped way up from 120 so aging (if
                         // implemented) has real runway to promote
                         // `starved` before the run ends.

// --- Donation-pair timeline, all in ticks from each thread's own start ---
#define LOW_PRELOCK_TICKS    40   // low spins this long BEFORE grabbing
                                   // the mutex -- long enough to force
                                   // several MLFQ demotions first.
#define LOW_HOLD_TICKS       60   // low then holds the mutex this long.
#define HIGH_PRELOCK_TICKS   50   // high spins this long before trying
                                   // to lock -- guaranteed to land
                                   // inside low's hold window
                                   // (LOW_PRELOCK_TICKS, LOW_PRELOCK_TICKS
                                   // + LOW_HOLD_TICKS) = (40, 100).

int mid;   // donation-target mutex (low holds it, high blocks on it)
int pmid;  // print-serializing mutex — output clarity only

char hog_stack[NHOGS][4096];
char io_stack[NIO][4096];
char starve_stack[4096];
char low_stack[4096];
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

// NOTE: this is the actual fix. Busy-spin in small chunks, checking
// uptime() between chunks, until `ticks` scheduler ticks have
// genuinely elapsed -- independent of host CPU speed. Every phase
// below is now expressed in ticks (uptime()), never in raw iteration
// counts, so the whole demo's timing is reproducible on any machine.
void
burn_ticks(int ticks)
{
  int start = uptime();
  while (uptime() - start < ticks)
    burn(50000);  // small enough to poll uptime() often, still keeps
                   // the CPU continuously busy (never voluntarily
                   // yields) so MLFQ sees it as CPU-bound the whole
                   // time.
}

// Never blocks voluntarily -> should demote to and settle at the
// bottom MLFQ queue.
void
cpu_hog(void *arg)
{
  int start = uptime();

  safe_print("[hog] starting, will spin for the whole run\n");
  while (uptime() - start < DURATION)
    burn(2000000);
  safe_print("[hog] done\n");
  thread_exit(0);
}

// Does a little work, then blocks before its quantum would expire,
// repeatedly -> should stay near (or return quickly to) the top queue.
void
io_bound(void *arg)
{
  int start = uptime();

  safe_print("[io] starting, works briefly then sleeps, repeatedly\n");
  while (uptime() - start < DURATION) {
    burn(20000);
    pause(2);
  }
  safe_print("[io] done\n");
  thread_exit(0);
}

// Deliberately does almost nothing but keeps asking to run, sitting
// behind the CPU hogs at the same low queue level. With no aging this
// thread would starve; with aging it should eventually get force-
// promoted back to the top queue after AGING_THRESHOLD ticks of
// waiting -- that's the jump-back-up you want to see in the chart.
//
// Runs for the full DURATION (was previously bounded by the same
// DURATION but DURATION itself was too short at 120 ticks for most
// aging thresholds to ever fire). Never sleeps, so it behaves like a
// hog to the scheduler and should get demoted right alongside them --
// the interesting thing to check is whether it EVER climbs back up
// before the run ends.
void
starved_worker(void *arg)
{
  int start = uptime();

  safe_print("[starved] starting, mostly just waiting behind the hogs\n");
  while (uptime() - start < DURATION)
    burn_ticks(1);
  safe_print("[starved] done\n");
  thread_exit(0);
}

void
low_priority_worker(void *arg)
{
  safe_print("[low] burning CPU to force demotion...\n");
  burn_ticks(LOW_PRELOCK_TICKS);
  safe_print("[low] now locking mutex (should be demoted by now)\n");
  mutex_lock(mid);
  safe_print("[low] got the mutex, holding it for a LONG time...\n");
  burn_ticks(LOW_HOLD_TICKS);
  safe_print("[low] releasing the mutex\n");
  mutex_unlock(mid);
  thread_exit(0);
}

void
high_priority_worker(void *arg)
{
  // Arrives at tick HIGH_PRELOCK_TICKS (50), which is safely inside
  // low's hold window of (LOW_PRELOCK_TICKS, LOW_PRELOCK_TICKS +
  // LOW_HOLD_TICKS) = (40, 100) -- guaranteed contention, regardless
  // of host speed.
  burn_ticks(HIGH_PRELOCK_TICKS);
  safe_print("[high] trying to lock (should block on low's held mutex)...\n");
  mutex_lock(mid);
  safe_print("[high] got the mutex!\n");
  mutex_unlock(mid);
  thread_exit(0);
}

int
main(int argc, char *argv[])
{
  int hog_tid[NHOGS], io_tid[NIO], starve_tid, low_tid, high_tid;
  int i;

  mid = mutex_create();
  pmid = mutex_create();
  if (mid < 0 || pmid < 0) {
    printf("don_super: mutex_create failed\n");
    exit(1);
  }

  printf("don_super: launching %d hogs, %d io threads, "
         "1 starved thread, low/high donation pair (~%d ticks)\n",
         NHOGS, NIO, DURATION);

  for (i = 0; i < NHOGS; i++)
    hog_tid[i] = thread_create(cpu_hog, (void *)(long)i,
                                hog_stack[i] + sizeof(hog_stack[i]) - 16);

  for (i = 0; i < NIO; i++)
    io_tid[i] = thread_create(io_bound, (void *)(long)i,
                               io_stack[i] + sizeof(io_stack[i]) - 16);

  starve_tid = thread_create(starved_worker, 0,
                              starve_stack + sizeof(starve_stack) - 16);

  low_tid = thread_create(low_priority_worker, 0,
                           low_stack + sizeof(low_stack) - 16);
  high_tid = thread_create(high_priority_worker, 0,
                            high_stack + sizeof(high_stack) - 16);

  for (i = 0; i < NHOGS; i++)
    thread_join(hog_tid[i]);
  for (i = 0; i < NIO; i++)
    thread_join(io_tid[i]);
  thread_join(starve_tid);
  thread_join(low_tid);
  thread_join(high_tid);

  printf("don_super: all threads joined. Now run:\n");
  printf("  schedstat_dump sched.csv\n");
  printf("to pull the ring buffer this run just filled.\n");

  exit(0);
}