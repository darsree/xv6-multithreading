// starvetest.c — isolates ONE clean aging/promotion event.
//
// 2 CPU-bound hogs monopolize the CPUs. 1 "starved" thread wants to
// run but keeps getting bumped by the hogs at the same queue level.
// Your AGING_THRESHOLD is 100 ticks (kernel/sched.h) -- so once the
// starved thread has waited ~100 ticks without getting a turn, MLFQ
// should force-promote it back toward queue level 0, regardless of
// what the hogs are doing.
//
// Unlike don_super (which mixes in I/O threads and donation at the
// same time), this test isolates JUST the hog-vs-starved dynamic, so
// the promotion shows up as one unambiguous jump-back-up in the
// priority panel, easy to point at on a slide.
//
// Usage (inside the xv6 shell):
//   $ starvetest
//   $ schedstat_dump starve.csv
// then plot it: python3 tools/viz/plot_timeline.py starve.csv -o starve.png
// Look for the starved thread's queue_level jumping DOWN (toward 0)
// around tick (start_tick + 100), while the hogs stay pinned high.

#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define NHOGS    2
#define DURATION 220   // > 2x AGING_THRESHOLD, so you can see at least
                        // one full starve -> promote -> re-demote cycle

int pmid;

char hog_stack[NHOGS][4096];
char starve_stack[4096];

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
cpu_hog(void *arg)
{
  int start = uptime();
  safe_print("[hog] starting, will spin for the whole run\n");
  while (uptime() - start < DURATION)
    burn(2000000);
  safe_print("[hog] done\n");
  thread_exit(0);
}

// Deliberately CPU-hungry too (never voluntarily sleeps), so it
// competes with the hogs directly instead of quietly blocking. That's
// the point: without aging, this thread could wait behind the hogs
// indefinitely.
void
starved_worker(void *arg)
{
  int start = uptime();
  safe_print("[starved] starting -- watch this one's queue_level\n");
  while (uptime() - start < DURATION)
    burn(500000);
  safe_print("[starved] done\n");
  thread_exit(0);
}

int
main(int argc, char *argv[])
{
  int hog_tid[NHOGS], starve_tid;
  int i;

  pmid = mutex_create();
  if (pmid < 0) {
    printf("starvetest: mutex_create failed\n");
    exit(1);
  }

  printf("starvetest: %d hogs vs 1 starved thread, ~%d ticks, "
         "AGING_THRESHOLD=100\n", NHOGS, DURATION);

  for (i = 0; i < NHOGS; i++)
    hog_tid[i] = thread_create(cpu_hog, (void *)(long)i,
                                hog_stack[i] + sizeof(hog_stack[i]) - 16);

  starve_tid = thread_create(starved_worker, 0,
                              starve_stack + sizeof(starve_stack) - 16);

  for (i = 0; i < NHOGS; i++)
    thread_join(hog_tid[i]);
  thread_join(starve_tid);

  printf("starvetest: done. Now run: schedstat_dump starve.csv\n");
  exit(0);
}