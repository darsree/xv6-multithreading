// schedtest.c — Member 3 deliverable
// Mix of a CPU-bound spin-loop process and a sleep/wake-heavy process.
// The scheduler itself (kernel/sched.c) logs every queue-level change
// and every aging/starvation promotion straight to the console as
// "mlfq: ..." lines — watch the QEMU console while this runs to see:
//   - the CPU-bound hogs demote and settle at the bottom queue
//   - the I/O-bound process stay near the top queue the whole time
//   - the "victim" (forked last, so it ends up scanned last among
//     same-level procs) periodically get force-promoted once it's
//     waited past AGING_THRESHOLD ticks behind the hogs
//
// Usage:
//   schedtest
#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define NHOGS    3
#define DURATION 200 // ticks; clockintr fires ~10x/sec, so ~20s

static void
burn(long n)
{
  volatile long i;
  for (i = 0; i < n; i++)
    ;
}

// Never blocks voluntarily: pure CPU-bound behavior. Should demote to
// (and settle at) the bottom MLFQ queue.
static void
cpu_hog(char *label)
{
  int start = uptime();

  printf("schedtest: %s pid=%d starting (spins for the whole run)\n", label,
         getpid());
  while (uptime() - start < DURATION)
    burn(2000000);
  printf("schedtest: %s pid=%d done\n", label, getpid());
  exit(0);
}

// Does a little work, then blocks before its quantum would expire,
// repeatedly. Should stay near (or return quickly to) the top queue.
static void
io_bound(void)
{
  int start = uptime();
  int wakes = 0;

  printf("schedtest: io-bound pid=%d starting (works briefly, then sleeps)\n",
         getpid());
  while (uptime() - start < DURATION) {
    burn(20000);
    pause(2);
    wakes++;
  }
  printf("schedtest: io-bound pid=%d done, %d wake cycles\n", getpid(), wakes);
  exit(0);
}

int
main(int argc, char *argv[])
{
  int i, n = 0;

  printf("schedtest: CPU-bound vs I/O-bound MLFQ mix, ~%d ticks (~%ds)\n",
         DURATION, DURATION / 10);
  printf(
      "schedtest: watch the console for 'mlfq:' lines logged by the kernel\n");

  // Background CPU-bound load: always-runnable hogs. These settle at
  // the bottom queue quickly and are exactly what a later, equally
  // CPU-bound process could starve behind without aging.
  for (i = 0; i < NHOGS; i++) {
    int pid = fork();
    if (pid == 0)
      cpu_hog("hog");
    n++;
  }

  // I/O-bound process.
  {
    int pid = fork();
    if (pid == 0)
      io_bound();
    n++;
  }

  // Forked last -> allocated the latest proc-table slot -> scanned
  // last among same-level RUNNABLE procs by sched_pick_next(). This is
  // the one to watch for periodic "STARVED level X->0" promotions.
  {
    int pid = fork();
    if (pid == 0) {
      printf("schedtest: victim pid=%d starting (watch for it getting "
             "aged/promoted)\n",
             getpid());
      cpu_hog("victim");
    }
    n++;
  }

  for (i = 0; i < n; i++)
    wait(0);

  printf("schedtest: done\n");
  exit(0);
}
