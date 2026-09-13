// schedstat_dump.c — Member 4 deliverable
// Dumps the getschedstat ring buffer as CSV, either to a file directly
// (if given a filename) or to the console (for the manual capture
// workflow via `make qemu-nox | tee console.log`).
//
// Usage inside xv6:
//   $ schedstat_dump             (prints CSV to the console)
//   $ schedstat_dump sched.csv   (writes CSV straight to sched.csv on the xv6 fs)
//
// If you saved straight to a file inside xv6, that file only exists
// on xv6's own virtual disk (fs.img) -- your host machine can't read
// it directly, so `cat` it back out to the console (or just skip the
// filename entirely and use the console workflow below).
//
//   $ make qemu-nox 2>&1 | tee console.log
//   (run `schedstat_dump` with no args inside xv6, quit qemu)
//   $ grep -E '^[0-9]+,[0-9]+,[0-9]+,[0-9]+,[0-9]+,[0-9]+,[0-9]+$' console.log > schedstat.csv
//
// Prefer that grep over matching the CSV header line with sed: on a
// multi-core run, another CPU's kernel log output can interleave into
// the middle of the header line and corrupt it, silently breaking a
// header-anchored sed range. Matching the plain 7-integer data-line
// shape survives that. plot_timeline.py already falls back to the
// right column names when no header line is present.

#include "kernel/types.h"
#include "kernel/fcntl.h"
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

  int fd = 1;           // default: stdout (console)
  int opened_file = 0;

  if (argc > 1) {
    fd = open(argv[1], O_CREATE | O_WRONLY);
    if (fd < 0) {
      printf("schedstat_dump: cannot open %s for writing\n", argv[1]);
      exit(1);
    }
    opened_file = 1;
  }

  // CSV header first, so plot_timeline.py can read this straight with
  // pandas.read_csv() once it's isolated from surrounding console noise
  // (or directly, if written straight to a file).
  fprintf(fd, "tick,tgid,tid,is_thread,state,cpu_id,priority\n");
  for (int i = 0; i < n; i++) {
    fprintf(fd, "%d,%d,%d,%d,%d,%d,%d\n",
            buf[i].tick, buf[i].tgid, buf[i].tid, buf[i].is_thread,
            buf[i].state, buf[i].cpu_id, buf[i].priority);
  }
  // Sentinel line so a host-side script can find the end of the CSV
  // block inside a noisy console capture (only needed for the console
  // path, but harmless to always include -- plot_timeline.py already
  // skips non-data lines like this one).
  fprintf(fd, "END_SCHEDSTAT\n");

  if (opened_file) {
    close(fd);
    printf("schedstat_dump: wrote %d records to %s\n", n, argv[1]);
  }

  exit(0);
}