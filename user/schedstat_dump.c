// schedstat_dump.c — Member 4 deliverable
// Dumps the getschedstat ring buffer as CSV, so it can be captured
// from the QEMU console and fed to tools/viz/plot_timeline.py.
//
// Usage inside xv6:
//   $ schedstat_dump
// Usage from the host, to capture it to a file for plotting:
//   $ make qemu-nox 2>&1 | tee console.log
//   (run `schedstat_dump` inside xv6, quit qemu)
//   $ sed -n '/tick,tgid,tid,is_thread,state,cpu_id,priority/,/END_SCHEDSTAT/p' console.log | head -n -1 > schedstat.csv

#include "kernel/types.h"
#include "user/user.h"
#include "user/schedstat.h"

struct schedstat_rec buf[SCHEDSTAT_RINGSIZE];

int
main(int argc, char *argv[])
{
  int n = getschedstat(buf, SCHEDSTAT_RINGSIZE);

  if (n < 0) {
    printf("schedstat_dump: getschedstat failed\n");
    exit(1);
  }

  // CSV header first, so plot_timeline.py can read this straight with
  // pandas.read_csv() once it's isolated from surrounding console noise.
  printf("tick,tgid,tid,is_thread,state,cpu_id,priority\n");
  for (int i = 0; i < n; i++) {
    printf("%d,%d,%d,%d,%d,%d,%d\n",
           buf[i].tick, buf[i].tgid, buf[i].tid, buf[i].is_thread,
           buf[i].state, buf[i].cpu_id, buf[i].priority);
  }
  // Sentinel line so a host-side script can find the end of the CSV
  // block inside a noisy console capture (see usage note above).
  printf("END_SCHEDSTAT\n");

  exit(0);
}