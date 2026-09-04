# Skeleton Commit — frozen Day 0

## struct proc additions (kernel/proc.h)

```c
int tgid;
int tid;
int is_thread;
uint64 ustack_base;
void *retval;
int *pgrefcnt;
int priority;         // for MLFQ
int queue_level;
int wait_ticks;        // for aging
int quantum;            // for adaptive quantum
int donated_priority;    // for donation
```

## Stub prototypes (bodies: `return 0;` or `panic("todo");`)

See kernel/thread.h, kernel/sync.h, kernel/sched.h, kernel/donate.h,
kernel/schedstat.h for the per-owner headers. Once this compiles and boots
on everyone's machine, work splits cleanly — do not change these
signatures without a team sync.
