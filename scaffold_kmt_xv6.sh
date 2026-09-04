#!/usr/bin/env bash
# scaffold_kmt_xv6.sh
#
# Run this from INSIDE your cloned xv6-riscv repo (the "kmt-xv6" checkout
# from Step 0). It only creates new files/dirs — it never touches existing
# xv6 files like kernel/proc.c, kernel/syscall.h, etc.
#
# Usage:
#   cd kmt-xv6
#   bash scaffold_kmt_xv6.sh

set -euo pipefail

if [ ! -d "kernel" ] || [ ! -d "user" ]; then
  echo "error: run this from the root of your xv6-riscv checkout (kernel/ and user/ not found)"
  exit 1
fi

echo "Scaffolding kmt-xv6 project layout..."

# Safety net: never clobber a file that already exists (e.g. xv6 ships its
# own kernel/stat.h for struct stat — that's why M4's file below is named
# schedstat.h/.c, not stat.h/.c). If any target already exists, stop before
# writing anything.
NEW_FILES="
kernel/thread.h kernel/thread.c
kernel/sync.h kernel/sync.c
kernel/sched.h kernel/sched.c
kernel/donate.h kernel/donate.c
kernel/smp_balance.h kernel/smp_balance.c
kernel/schedstat.h kernel/schedstat.c
user/ttest_basic.c user/synctest.c user/schedtest.c user/schedstat_dump.c
tools/viz/plot_timeline.py tools/viz/requirements.txt
docs/SKELETON.md docs/SYSCALL_TABLE.md docs/API_FREEZE.md docs/INTEGRATION.md
.github/CODEOWNERS
"
for f in $NEW_FILES; do
  if [ -e "$f" ]; then
    echo "error: $f already exists — refusing to overwrite. Remove it or check you haven't already scaffolded."
    exit 1
  fi
done

# ---------------------------------------------------------------------------
# kernel/ — one new file pair per member, no touching of existing xv6 files
# ---------------------------------------------------------------------------

# --- Member 1: Thread Core & Lifecycle ---
cat > kernel/thread.h << 'EOF'
// thread.h — Member 1: thread core & lifecycle
// Public API for thread creation/exit/join and group teardown.
// FREEZE these signatures by end of Week 1 — everyone else's stubs
// depend on them staying stable.
#ifndef THREAD_H
#define THREAD_H

int  thread_create(void (*fcn)(void*), void *arg, void *stack);
int  thread_join(int tid);
void thread_exit(void *retval);

// Called from exit()/kill() to tear down every proc sharing a tgid.
void thread_group_teardown(int tgid);

#endif // THREAD_H
EOF

cat > kernel/thread.c << 'EOF'
// thread.c — Member 1: thread core & lifecycle
//
// Owns:
//   - tgid/tid assignment in allocproc()
//   - pagetable refcounting (pgrefcnt) on create/exit
//   - per-thread user stack allocation
//   - thread_create / thread_exit / thread_join
//   - group-wide teardown on exit()/kill()
//
// Do not put scheduler, sync, or stats logic in this file.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "thread.h"

int
thread_create(void (*fcn)(void*), void *arg, void *stack)
{
  // TODO(M1): clone trapframe setup pattern from fork() minus uvmcopy();
  // set epc, sp, a0. Increment pgrefcnt on the shared pagetable.
  panic("thread_create: not implemented");
}

void
thread_exit(void *retval)
{
  // TODO(M1): store retval, mark ZOMBIE, wake joiners on tgid channel,
  // decrement pgrefcnt; only call uvmfree() when refcount hits 0.
  panic("thread_exit: not implemented");
}

int
thread_join(int tid)
{
  // TODO(M1): sleep/wake on tgid channel, reap slot, return retval.
  return -1;
}

void
thread_group_teardown(int tgid)
{
  // TODO(M1): walk proc[] and terminate every proc sharing this tgid.
}
EOF

# --- Member 2: Synchronization Primitives ---
cat > kernel/sync.h << 'EOF'
// sync.h — Member 2: synchronization primitives
#ifndef SYNC_H
#define SYNC_H

#include "spinlock.h"

#define NKMUTEX 64
#define NKCV    64

struct kmutex {
  struct spinlock lk;
  int locked;
  int owner;   // pid/tid of holder, -1 if unlocked (used by M4's donation)
  int valid;
};

struct cv {
  int valid;
  // wait channel is simply &cv[i]; no extra state needed for basic impl
};

void kmutex_init(void);
int  kmutex_create(void);
int  kmutex_lock(int id);
int  kmutex_unlock(int id);
int  kmutex_destroy(int id);

int  cv_wait(int cv_id, int mutex_id);
int  cv_signal(int cv_id);
int  cv_broadcast(int cv_id);

// Optional: semaphore as thin wrapper over mutex + cv
int  sem_create(int count);
int  sem_wait(int id);
int  sem_post(int id);

#endif // SYNC_H
EOF

cat > kernel/sync.c << 'EOF'
// sync.c — Member 2: synchronization primitives
//
// Owns: kmutex array + cv array, syscall bodies for mutex/cv/(sem).
// Talks to sleep()/wakeup() already in xv6 core — does not depend on
// Member 1's thread.c being finished.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "sync.h"

static struct kmutex mutexes[NKMUTEX];
static struct cv cvs[NKCV];

void
kmutex_init(void)
{
  // TODO(M2): initspin each mutex lock, mark all slots invalid/free.
}

int
kmutex_create(void)
{
  // TODO(M2): find a free slot, mark valid, return index (mirrors fd table).
  return -1;
}

int
kmutex_lock(int id)
{
  // TODO(M2): acquire spinlock, sleep while locked, set locked=1, owner=self.
  return -1;
}

int
kmutex_unlock(int id)
{
  // TODO(M2): set locked=0, owner=-1, wakeup waiters.
  return -1;
}

int
kmutex_destroy(int id)
{
  return -1;
}

int
cv_wait(int cv_id, int mutex_id)
{
  // TODO(M2): release mutex, sleep on &cvs[cv_id], reacquire mutex on wake.
  return -1;
}

int
cv_signal(int cv_id)
{
  // TODO(M2): wakeup one waiter on &cvs[cv_id].
  return -1;
}

int
cv_broadcast(int cv_id)
{
  // TODO(M2): wakeup all waiters on &cvs[cv_id].
  return -1;
}

int
sem_create(int count)
{
  // Optional wrapper over kmutex + cv.
  return -1;
}

int
sem_wait(int id)
{
  return -1;
}

int
sem_post(int id)
{
  return -1;
}
EOF

# --- Member 3: Scheduler (MLFQ + Aging + Adaptive Quantum) ---
cat > kernel/sched.h << 'EOF'
// sched.h — Member 3: MLFQ scheduler
#ifndef SCHED_H
#define SCHED_H

#include "proc.h"

#define NQUEUES 4
#define AGING_THRESHOLD 100  // ticks a proc can wait before force-promote

// Called once per timer tick from trap.c for bookkeeping (aging, quantum
// expiry). Keep this the ONLY hook trap.c needs to call.
void mlfq_tick(struct proc *p);

// Called from proc.c's scheduler() loop to pick the next proc to run.
// Keeps the ~50 lines of MLFQ logic out of proc.c.
struct proc *sched_pick_next(void);

#endif // SCHED_H
EOF

cat > kernel/sched.c << 'EOF'
// sched.c — Member 3: MLFQ + aging + adaptive quantum
//
// Owns: multi-level feedback queue logic, quantum expiry/demotion,
// aging/promotion, adaptive quantum via EMA of recent CPU usage.
// Independent of M1/M2's actual implementations — only needs the
// priority/queue_level/wait_ticks/quantum fields already in struct proc.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "sched.h"

void
mlfq_tick(struct proc *p)
{
  // TODO(M3): increment wait_ticks for READY-not-running procs; force
  // promote past AGING_THRESHOLD. Update adaptive quantum EMA for the
  // currently running proc.
}

struct proc *
sched_pick_next(void)
{
  // TODO(M3): scan queue_level 0..NQUEUES-1, return first RUNNABLE proc.
  return 0;
}
EOF

# --- Member 4: Priority Donation + SMP Load Balancing + Instrumentation ---
cat > kernel/donate.h << 'EOF'
// donate.h — Member 4: priority donation
#ifndef DONATE_H
#define DONATE_H

// Boost the mutex owner's priority to the blocker's priority.
// Build/test first against a hand-rolled dummy mutex; swap to M2's
// real struct kmutex at Week 3 integration.
void donate_boost(int owner_pid, int blocker_priority);
void donate_restore(int pid);

#endif // DONATE_H
EOF

cat > kernel/donate.c << 'EOF'
// donate.c — Member 4: priority donation
//
// Depends conceptually on M2's kmutex.owner field (already in the
// skeleton) but should be built/tested against your own stub mutex
// until Week 3 integration.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "donate.h"

void
donate_boost(int owner_pid, int blocker_priority)
{
  // TODO(M4): find proc by pid, save donated_priority, boost effective priority.
}

void
donate_restore(int pid)
{
  // TODO(M4): restore proc's original priority on mutex unlock.
}
EOF

cat > kernel/smp_balance.h << 'EOF'
// smp_balance.h — Member 4: per-CPU run queues + load balancing (optional)
#ifndef SMP_BALANCE_H
#define SMP_BALANCE_H

// Called from scheduler()'s idle path. Migrates one proc if imbalance
// across per-CPU queues exceeds a threshold. Build against M3's stub
// scheduler shape; swap to real MLFQ queues at integration.
void smp_balance_check(void);

#endif // SMP_BALANCE_H
EOF

cat > kernel/smp_balance.c << 'EOF'
// smp_balance.c — Member 4: per-CPU run queues + load balancing (optional)
//
// Cut first if time-pressured — this is the stretch item.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "smp_balance.h"

void
smp_balance_check(void)
{
  // TODO(M4, optional): partition proc[] by p->cpu_affinity; migrate one
  // proc if load imbalance exceeds threshold.
}
EOF

cat > kernel/schedstat.h << 'EOF'
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
EOF

cat > kernel/schedstat.c << 'EOF'
// schedstat.c — Member 4: scheduler instrumentation (getschedstat)
//
// Ring buffer logging (tick, tgid, tid, is_thread, state, cpu_id,
// priority) on every context switch. Self-contained: only needs a
// single hook call inserted near swtch() in proc.c.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "schedstat.h"

static struct schedstat_rec ring[SCHEDSTAT_RINGSIZE];
static int ring_head = 0;

void
record_schedstat(struct proc *p, int cpu_id)
{
  // TODO(M4): fill ring[ring_head] from p's fields + cpu_id, advance head.
}

int
getschedstat(void *buf, int max)
{
  // TODO(M4): copyout up to `max` records from ring buffer to user buf.
  return -1;
}
EOF

# ---------------------------------------------------------------------------
# user/ — one test program per member
# ---------------------------------------------------------------------------

cat > user/ttest_basic.c << 'EOF'
// ttest_basic.c — Member 1 deliverable
// Spawns N threads, each writes to a shared global, joins all, exits
// cleanly. Also verify: `kill -9` mid-run correctly reaps the whole group.
#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  printf("ttest_basic: TODO implement thread spawn/join test\n");
  exit(0);
}
EOF

cat > user/synctest.c << 'EOF'
// synctest.c — Member 2 deliverable
// Producer/consumer test using kmutex + cv. Can run against plain
// fork()'d processes before thread_create is ready.
#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  printf("synctest: TODO implement producer/consumer test\n");
  exit(0);
}
EOF

cat > user/schedtest.c << 'EOF'
// schedtest.c — Member 3 deliverable
// Mix of a CPU-bound spin-loop process and a sleep/wake-heavy process.
// Log queue level over time; show CPU-bound demotes/settles while
// I/O-bound stays responsive; show starvation/promotion for a
// long-waiting low-priority process.
#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  printf("schedtest: TODO implement CPU-bound vs I/O-bound mix test\n");
  exit(0);
}
EOF

cat > user/schedstat_dump.c << 'EOF'
// schedstat_dump.c — Member 4 deliverable
// Dumps the getschedstat ring buffer to console/CSV for tools/viz to plot.
#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  printf("schedstat_dump: TODO call getschedstat() and print CSV\n");
  exit(0);
}
EOF

# ---------------------------------------------------------------------------
# tools/viz — Member 4's external visualization script
# ---------------------------------------------------------------------------

mkdir -p tools/viz
cat > tools/viz/requirements.txt << 'EOF'
matplotlib
pandas
EOF

cat > tools/viz/plot_timeline.py << 'EOF'
#!/usr/bin/env python3
"""plot_timeline.py — Member 4

Reads schedstat CSV (from user/schedstat_dump.c output, captured via
QEMU console redirect) and renders a Gantt-style timeline colored by
tgid (process-vs-thread view) and queue level (scheduler-behavior view).

Usage:
    python3 plot_timeline.py schedstat.csv
"""
import sys

def main():
    if len(sys.argv) < 2:
        print("usage: plot_timeline.py <schedstat.csv>")
        sys.exit(1)
    # TODO(M4): pandas.read_csv + matplotlib broken_barh Gantt chart.
    print("plot_timeline: TODO implement Gantt chart rendering")

if __name__ == "__main__":
    main()
EOF
chmod +x tools/viz/plot_timeline.py

# ---------------------------------------------------------------------------
# docs/ — shared reference docs (frozen agreements live here, not in code)
# ---------------------------------------------------------------------------

mkdir -p docs

cat > docs/SKELETON.md << 'EOF'
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
EOF

cat > docs/SYSCALL_TABLE.md << 'EOF'
# Reserved syscall number block: 22–35

| # | Name | Owner |
|---|---|---|
| 22 | thread_create | M1 |
| 23 | thread_join | M1 |
| 24 | thread_exit | M1 |
| 25 | mutex_create | M2 |
| 26 | mutex_lock | M2 |
| 27 | mutex_unlock | M2 |
| 28 | cv_wait | M2 |
| 29 | cv_signal / cv_broadcast | M2 |
| 30–31 | (reserved, M3 — likely unused, scheduler has no new syscalls) | M3 |
| 32 | getschedstat | M4 |
| 33–35 | (reserved buffer) | — |

Rule: each member appends ONLY their own rows to syscall.h /
sysproc.c / usys.pl / user/user.h. Never renumber or reflow existing
entries.
EOF

cat > docs/API_FREEZE.md << 'EOF'
# API Freeze — end of Week 1

M1 freezes these signatures (from kernel/thread.h). Everyone else
codes against the signature, not the implementation, from this point
on:

```c
int  thread_create(void (*fcn)(void*), void *arg, void *stack);
int  thread_join(int tid);
void thread_exit(void *retval);
```

Record any agreed changes here with a date + reason. Do not silently
change signatures after this point.
EOF

cat > docs/INTEGRATION.md << 'EOF'
# Week 3 Integration Checklist

1. [ ] M1's thread.c API is frozen (no signature changes) — confirm.
2. [ ] M4 replaces the hand-rolled dummy mutex in donate.c with a real
       struct kmutex reference from sync.h.
3. [ ] M4 replaces the stub scheduler shape with M3's real queue
       struct from sched.h.
4. [ ] Run all four test programs (ttest_basic, synctest, schedtest,
       schedstat_dump + plot_timeline.py) back to back on the merged
       tree.
5. [ ] Merge `integration` branch into `main`; tag `week3-integration`.
EOF

# ---------------------------------------------------------------------------
# CODEOWNERS — optional, auto-requests reviews per path (GitHub only)
# ---------------------------------------------------------------------------

mkdir -p .github
cat > .github/CODEOWNERS << 'EOF'
# Auto-request reviews from the right owner. Replace @handles below.
kernel/thread.c        @member1
kernel/thread.h        @member1
kernel/sync.c          @member2
kernel/sync.h          @member2
kernel/sched.c         @member3
kernel/sched.h         @member3
kernel/donate.c        @member4
kernel/donate.h        @member4
kernel/smp_balance.c   @member4
kernel/smp_balance.h   @member4
kernel/schedstat.c     @member4
kernel/schedstat.h     @member4
user/ttest_basic.c     @member1
user/synctest.c        @member2
user/schedtest.c       @member3
user/schedstat_dump.c  @member4
tools/viz/             @member4
EOF

echo ""
echo "Done. New files created:"
echo "  kernel/{thread,sync,sched,donate,smp_balance,schedstat}.{c,h}"
echo "  user/{ttest_basic,synctest,schedtest,schedstat_dump}.c"
echo "  tools/viz/{plot_timeline.py,requirements.txt}"
echo "  docs/{SKELETON,SYSCALL_TABLE,API_FREEZE,INTEGRATION}.md"
echo "  .github/CODEOWNERS"
echo ""
echo "Next steps:"
echo "  1. Add the new .c files to your Makefile's OBJS list (kernel and user)."
echo "  2. Add the struct proc fields from docs/SKELETON.md to kernel/proc.h."
echo "  3. Reserve syscall numbers per docs/SYSCALL_TABLE.md in kernel/syscall.h."
echo "  4. Commit as the 'skeleton commit', push, have all 4 members pull + build."
