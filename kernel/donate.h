// donate.h — Member 4: priority donation
#ifndef DONATE_H
#define DONATE_H

// Boost the mutex owner's priority to the blocker's priority.
// Build/test first against a hand-rolled dummy mutex; swap to M2's
// real struct kmutex at Week 3 integration.
void donate_boost(int owner_pid, int blocker_priority);
void donate_restore(int pid);

#endif // DONATE_H
