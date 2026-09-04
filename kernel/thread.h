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
