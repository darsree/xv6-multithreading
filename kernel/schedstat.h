// schedstat.h — Member 4: scheduler instrumentation
// NOTE: named schedstat.h, NOT stat.h — xv6 already has kernel/stat.h
// for the file-stat struct (used by fstat/ls). Never rename this to
// stat.h or you will silently break the filesystem syscalls.
#ifndef SCHEDSTAT_H
#define SCHEDSTAT_H

#define SCHEDSTAT_RINGSIZE 1024

struct schedstat_rec {
  uint  tick;
  int   tgid;
  int   tid;
  int   is_thread;
  int   state;
  int   cpu_id;
  int   priority;
};

// Self-contained addition to swtch/scheduler() — no dependency on
// anyone else's code.
void record_schedstat(struct proc *p, int cpu_id);

// Syscall body: copies up to `max` records into user buf, returns count.
int  getschedstat(void *buf, int max);

#endif // SCHEDSTAT_H
