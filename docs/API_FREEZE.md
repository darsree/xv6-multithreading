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
