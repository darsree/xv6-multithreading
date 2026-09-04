// smp_balance.h — Member 4: per-CPU run queues + load balancing (optional)
#ifndef SMP_BALANCE_H
#define SMP_BALANCE_H

// Called from scheduler()'s idle path. Migrates one proc if imbalance
// across per-CPU queues exceeds a threshold. Build against M3's stub
// scheduler shape; swap to real MLFQ queues at integration.
void smp_balance_check(void);

#endif // SMP_BALANCE_H
