// smp_balance.h — Member 4: per-CPU load tracking + load balancing
#ifndef SMP_BALANCE_H
#define SMP_BALANCE_H

// Load-difference (in procs) between the busiest CPU and an idle one
// before we bother migrating anything.
#define SMP_IMBALANCE_THRESHOLD 2

// Call once at boot, alongside schedstatinit().
void smp_balance_init(void);

// Call once per proc, from allocproc(), alongside mlfq_init_proc() and
// donate_init_proc().
void smp_balance_init_proc(struct proc *p);

// Call from scheduler() right after a proc is dispatched, so
// cpu_load[]/p->cpu_affinity stay accurate.
void smp_note_dispatch(struct proc *p, int cpu_id);

// Called from scheduler()'s idle path. Migrates one proc's affinity if
// imbalance across CPUs exceeds SMP_IMBALANCE_THRESHOLD.
void smp_balance_check(void);

#endif // SMP_BALANCE_H