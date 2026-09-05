#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "thread.h"
#include "sync.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep_prepare(&ticks);
    release(&tickslock);
    sleep();
    acquire(&tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// M1: int thread_create(void (*fcn)(void*), void *arg, void *stack);
uint64
sys_thread_create(void)
{
  uint64 fcn, arg, stack;

  argaddr(0, &fcn);
  argaddr(1, &arg);
  argaddr(2, &stack);

  return thread_create((void (*)(void *))fcn, (void *)arg, (void *)stack);
}

// M1: int thread_join(int tid);
uint64
sys_thread_join(void)
{
  int tid;

  argint(0, &tid);
  return thread_join(tid);
}

// M1: void thread_exit(void *retval);
uint64
sys_thread_exit(void)
{
  uint64 retval;

  argaddr(0, &retval);
  thread_exit((void *)retval);
  return 0; // not reached
}

// M2: int mutex_create(void);
uint64
sys_mutex_create(void)
{
  return kmutex_create();
}

// M2: int mutex_lock(int id);
uint64
sys_mutex_lock(void)
{
  int id;
  argint(0, &id);
  return kmutex_lock(id);
}

// M2: int mutex_unlock(int id);
uint64
sys_mutex_unlock(void)
{
  int id;
  argint(0, &id);
  return kmutex_unlock(id);
}

// M2: int cv_wait(int cv_id, int mutex_id);
uint64
sys_cv_wait(void)
{
  int cv_id, mutex_id;
  argint(0, &cv_id);
  argint(1, &mutex_id);
  return cv_wait(cv_id, mutex_id);
}

// M2: int cv_signal(int cv_id);
uint64
sys_cv_signal(void)
{
  int cv_id;
  argint(0, &cv_id);
  return cv_signal(cv_id);
}

// M2: int cv_broadcast(int cv_id);
uint64
sys_cv_broadcast(void)
{
  int cv_id;
  argint(0, &cv_id);
  return cv_broadcast(cv_id);
}

uint64
sys_cv_create(void)
{
    return cv_create();
}

uint64
sys_mutex_destroy(void)
{
  int id;
  argint(0, &id);
  return kmutex_destroy(id);
}
