// schedstat_dump.c — Member 4 deliverable
//
// Dumps the getschedstat() ring buffer as CSV.
//
// Usage:
//   schedstat_dump            print CSV to console only
//   schedstat_dump FILE       print CSV to console AND write it to FILE
//                              (FILE lives in the xv6 fs.img, e.g. "sched.csv")
//
// The console output is wrapped in BEGIN/END marker lines so a host-side
// script can pull just the CSV block out of the raw qemu -nographic output
// (see tools/run_and_plot.py).

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

  int fd = -1;
  if (argc > 1) {
    fd = open(argv[1], O_CREATE | O_WRONLY);
    if (fd < 0) {
      printf("schedstat_dump: cannot open %s\n", argv[1]);
      exit(1);
    }
  }

  printf("==SCHEDSTAT_CSV_BEGIN==\n");
  printf("tick,tgid,tid,is_thread,state,cpu_id,priority\n");
  if (fd >= 0)
    fprintf(fd, "tick,tgid,tid,is_thread,state,cpu_id,priority\n");

  for (int i = 0; i < n; i++) {
    struct schedstat_rec *r = &buf[i];
    printf("%d,%d,%d,%d,%d,%d,%d\n",
           r->tick, r->tgid, r->tid, r->is_thread, r->state, r->cpu_id, r->priority);
    if (fd >= 0)
      fprintf(fd, "%d,%d,%d,%d,%d,%d,%d\n",
              r->tick, r->tgid, r->tid, r->is_thread, r->state, r->cpu_id, r->priority);
  }
  printf("==SCHEDSTAT_CSV_END==\n");

  if (fd >= 0)
    close(fd);

  exit(0);
}