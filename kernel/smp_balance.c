// smp_balance.c — Member 4: per-CPU load tracking + soft-affinity
// load balancing (optional stretch item).
//
// NOTE ON SCOPE: M3's sched_pick_next() does a single global scan of
// proc[] by queue_level — any CPU can pick up any RUNNABLE proc, so
// this scheduler is already implicitly load-balanced (there's no
// per-CPU run queue to go unbalanced in the first place). Rewriting
// that into real per-CPU queues would mean rewriting M3's file, which
// is exactly why the team doc marks this item "cut first if
// time-pressured."
//
// What's implemented here instead is still real, just narrower in
// scope: we track which CPU each proc last ran on (p->cpu_affinity)
// and a live per-CPU load count. When a CPU goes idle, it checks
// whether some other CPU's affinity group is significantly heavier
// and, if so, reassigns ("migrates") one of that CPU's RUNNABLE procs
// to itself. This doesn't pin execution (sched_pick_next ignores
// affinity), but it keeps accurate per-CPU load stats — visible via
// getschedstat()'s cpu_id field and tools/viz's timeline — and gives
// a real hook to make sched_pick_next() affinity-aware later without
// touching M3's file today.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"
#include "smp_balance.h"

extern struct proc proc[NPROC];
extern uint ticks; // trap.c — used to rate-limit how often we re-check

static struct spinlock smp_lock;
static int cpu_load[NCPU]; // live count of procs whose cpu_affinity == that cpu

// Minimum ticks between balance checks. Without this, a CPU that's idle
// for a stretch calls smp_balance_check() on every single idle-loop pass
// (i.e. every timer tick) — since sched_pick_next() ignores affinity, a
// proc can get re-dispatched anywhere and flip its affinity right back,
// so an unthrottled check just thrashes the same proc back and forth
// (visible as dozens of repeated "migrating pid=X" log lines with the
// same load numbers, going nowhere). This cooldown is what actually
// stops that.
#define SMP_BALANCE_COOLDOWN_TICKS 20
static uint last_balance_tick = 0;

void
smp_balance_init(void)
{
  initlock(&smp_lock, "smp_balance");
}

// Called once per proc, from allocproc(), alongside donate_init_proc()
// and mlfq_init_proc().
void
smp_balance_init_proc(struct proc *p)
{
  p->cpu_affinity = -1; // unassigned until first dispatch
}

// Called from scheduler() right after a proc is chosen and dispatched
// on cpu_id. Keeps cpu_load[] and p->cpu_affinity in sync.
void
smp_note_dispatch(struct proc *p, int cpu_id)
{
  acquire(&smp_lock);
  if (p->cpu_affinity >= 0 && p->cpu_affinity != cpu_id)
    cpu_load[p->cpu_affinity]--;
  if (p->cpu_affinity != cpu_id)
    cpu_load[cpu_id]++;
  release(&smp_lock);
  p->cpu_affinity = cpu_id;
}

// Called from scheduler()'s idle path (this CPU found nothing
// RUNNABLE). If another CPU's affinity group is overloaded relative
// to this idle one, migrate one of its RUNNABLE procs here.
void
smp_balance_check(void)
{
  int me = cpuid();
  int busiest = -1, busiest_load = 0, my_load;
  struct proc *p;

  acquire(&smp_lock);
  if (ticks - last_balance_tick < SMP_BALANCE_COOLDOWN_TICKS) {
    release(&smp_lock);
    return;
  }
  my_load = cpu_load[me];
  for (int c = 0; c < NCPU; c++) {
    if (c == me)
      continue;
    if (cpu_load[c] > busiest_load) {
      busiest_load = cpu_load[c];
      busiest = c;
    }
  }
  release(&smp_lock);

  if (busiest < 0 || busiest_load - my_load < SMP_IMBALANCE_THRESHOLD)
    return;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == RUNNABLE && p->cpu_affinity == busiest) {
      printk("smp_balance: migrating pid=%d tid=%d cpu %d -> %d (load %d vs %d)\n",
             p->pid, p->tid, busiest, me, busiest_load, my_load);
      acquire(&smp_lock);
      cpu_load[busiest]--;
      cpu_load[me]++;
      last_balance_tick = ticks;
      release(&smp_lock);
      p->cpu_affinity = me;
      release(&p->lock);
      return;
    }
    release(&p->lock);
  }
}