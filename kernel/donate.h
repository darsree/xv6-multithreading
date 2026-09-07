// donate.h — Member 4: priority donation
#ifndef DONATE_H
#define DONATE_H

// Call once per proc, from allocproc(). Initializes donation state.
void donate_init_proc(struct proc *p);

// Boost the mutex owner's queue_level toward the blocker's, so the
// scheduler runs it sooner. blocker_level is the blocking thread's
// own p->queue_level.
void donate_boost(int owner_pid, int blocker_level);

// Restore the owner's queue_level after it releases the mutex.
void donate_restore(int pid);

#endif // DONATE_H