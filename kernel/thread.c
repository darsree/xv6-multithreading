// thread.c — Member 1: thread core & lifecycle
//
// Owns:
//   - tgid/tid assignment in allocproc()          [see kernel/proc.c]
//   - pagetable refcounting (pgrefcnt) on create/exit [see freeproc() in proc.c]
//   - per-thread user stack allocation
//   - thread_create / thread_exit / thread_join
//   - group-wide teardown on exit()/kill()
//
// Design: a thread does NOT get a copy of its creator's address space
// (that's what fork() does via uvmcopy()). Instead every proc always
// gets its own pagetable_t object (own page-directory frames, so each
// has an independent TRAPFRAME/TRAMPOLINE mapping — no cross-thread
// collision there), but the leaf PTEs covering the *shared* region
// [0, shared_hi) are mirrored to point at the SAME physical pages as
// the group leader (see uvmmirror() below), instead of being copied.
// p->pgrefcnt is a single kalloc()'d cell shared by every member of
// the tgid, counting how many procs still reference those physical
// pages; only the last one standing (see freeproc() in proc.c) is
// allowed to actually kfree() them. Each thread additionally gets a
// private per-thread stack, uniquely owned, layered on top of the
// shared region at [ustack_base, sz) in its own pagetable only.
//
// Do not put scheduler, sync, or stats logic in this file.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"
#include "thread.h"

// Protects *p->pgrefcnt updates across every proc that shares it.
// One lock for every group is fine at NPROC=64 scale.
struct spinlock thread_lock;

// Protects the check-then-sleep race between thread_join() and
// thread_group_teardown() reaping the same target out from under a
// joiner, and doubles as the wait channel both sleep on. A specific
// thread's own proc-struct address is NOT safe to use as that
// channel: freeproc() can reset (and the slot can be recycled for an
// unrelated process) between a joiner's "not zombie yet" check and
// its sleep_prepare() call. join_lock's address is static and never
// recycled, so it's always a valid, wakeable channel.
struct spinlock join_lock;

// Defined in proc.c; not previously exposed outside it.
extern struct proc *initproc;
extern struct proc proc[NPROC];

void
threadinit(void)
{
  initlock(&thread_lock, "thread");
  initlock(&join_lock, "thread_join");
}

// Map [0, sz) of `old` into `new`, pointing at the SAME physical
// pages (shared, not copied) with the same permissions. This is the
// "minus uvmcopy()" part: no kalloc()/memmove() per page, just extra
// PTEs in the new pagetable aimed at pages the leader already owns.
static int
uvmmirror(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  for (i = 0; i < sz; i += PGSIZE) {
    if ((pte = walk(old, i, 0)) == 0)
      continue;
    if ((*pte & PTE_V) == 0)
      continue;
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if (mappages(new, i, PGSIZE, pa, flags) != 0)
      goto err;
  }
  return 0;

err:
  // Just drop the PTEs we added so far — do NOT free the physical
  // pages, they're still owned by the leader / other threads.
  uvmunmap(new, 0, i / PGSIZE, 0);
  return -1;
}

int
thread_create(void (*fcn)(void *), void *arg, void *stack)
{
  struct proc *p = myproc();
  struct proc *np;
  uint64 sp, stackbase;

  // First thread spawned by this process: it becomes the group
  // leader. Lazily allocate the shared refcount cell — kalloc() only
  // hands out whole pages, so we use one page to hold a single int.
  if (p->pgrefcnt == 0) {
    int *cnt = (int *)kalloc();
    if (cnt == 0)
      return -1;
    *cnt = 1; // counts the leader itself
    p->pgrefcnt = cnt;
    p->tgid = p->pid; // already true from allocproc(), kept explicit
  }

  // allocproc() hands back a proc with a fresh pagetable that
  // already has trampoline+trapframe mapped, and p->lock held.
  if ((np = allocproc()) == 0)
    return -1;

  // Share the leader's memory instead of copying it.
  if (uvmmirror(p->pagetable, np->pagetable, p->sz) < 0) {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // Private per-thread user stack, allocated above the shared region.
  if (stack == 0) {
    uint64 newsz = uvmalloc(np->pagetable, np->sz,
                             np->sz + USERSTACK * PGSIZE, PTE_W);
    if (newsz == 0) {
      freeproc(np);
      release(&np->lock);
      return -1;
    }
    stackbase = np->sz; // boundary between shared region and this
                         // thread's own private stack
    np->sz = newsz;
    sp = newsz;
  } else {
    // Caller supplied its own stack (already mapped somewhere in the
    // shared region, e.g. via sbrk() before calling thread_create()).
    // Nothing private for us to own/free here.
    stackbase = np->sz;
    sp = (uint64)stack;
  }
  np->ustack_base = stackbase;

  // Set up the new thread to start at fcn(arg) on its own stack.
  // Same trapframe-cloning idea as fork(), just without uvmcopy():
  // start from the leader's trapframe as a template, then override
  // exactly the three registers that matter for a fresh thread.
  *(np->trapframe) = *(p->trapframe);
  np->trapframe->epc = (uint64)fcn;
  np->trapframe->sp = sp;
  np->trapframe->a0 = (uint64)arg;

  // Thread identity: shares the leader's tgid, gets its own tid
  // (reusing its already-unique pid), shares the refcount cell.
  np->tgid = p->tgid;
  np->tid = np->pid;
  np->is_thread = 1;
  np->pgrefcnt = p->pgrefcnt;

  acquire(&thread_lock);
  (*np->pgrefcnt)++;
  release(&thread_lock);

  // Inherit open files / cwd like fork() does, so printf() etc.
  // still work from inside a thread.
  for (int i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);
  safestrcpy(np->name, p->name, sizeof(p->name));

  int tid = np->tid;
  np->state = RUNNABLE;
  release(&np->lock);

  return tid;
}

void
thread_exit(void *retval)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("initproc thread_exit");

  // Close files/cwd exactly like a normal exit would — every proc
  // (thread or not) owns its own ofile[]/cwd, these are never shared.
  for (int fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd]) {
      fileclose(p->ofile[fd]);
      p->ofile[fd] = 0;
    }
  }
  if (p->cwd) {
    begin_op();
    iput(p->cwd);
    end_op();
    p->cwd = 0;
  }

  acquire(&p->lock);
  p->retval = retval;
  p->xstate = 0;
  p->state = ZOMBIE;
  release(&p->lock);

  // Wake anyone in thread_join() or thread_group_teardown() waiting
  // on this or any other thread finishing. They all sleep on
  // join_lock's own stable address and re-check the actual proc
  // table state on wake, so a broadcast-style wakeup here is safe.
  wakeup(&join_lock);

  acquire(&p->lock);
  sched();
  panic("thread_exit: zombie thread returned");
}

int
thread_join(int tid)
{
  struct proc *me = myproc();
  struct proc *t;

  acquire(&join_lock);
  for (;;) {
    t = 0;
    for (struct proc *pp = proc; pp < &proc[NPROC]; pp++) {
      if (pp->is_thread && pp->tgid == me->tgid && pp->tid == tid) {
        t = pp;
        break;
      }
    }
    if (t == 0) {
      // No such thread in this group: either a bad tid, or it was
      // already reaped (by us, or by a concurrent
      // thread_group_teardown()).
      release(&join_lock);
      return -1;
    }

    acquire(&t->lock);
    if (t->state == ZOMBIE) {
      freeproc(t); // pgrefcnt-aware teardown, see proc.c
      release(&t->lock);
      release(&join_lock);
      return 0;
    }
    release(&t->lock);

    // Not done yet. join_lock stays held right up until
    // sleep_prepare() registers our wait — anyone about to reap a
    // ZOMBIE thread (us on a later loop, or thread_group_teardown())
    // must also go through join_lock, so the target can't vanish out
    // from under us in this gap.
    sleep_prepare(&join_lock);
    release(&join_lock);
    sleep();
    acquire(&join_lock);
  }
}

void
thread_group_teardown(int tgid)
{
  struct proc *me = myproc();
  int pending;

  // Phase 1: mark every other member of the group killed, and wake
  // any that are sleeping so they notice next time they'd block.
  for (struct proc *pp = proc; pp < &proc[NPROC]; pp++) {
    if (pp == me)
      continue;
    acquire(&pp->lock);
    if (pp->tgid == tgid && pp->state != UNUSED) {
      pp->killed = 1;
      if (pp->state == SLEEPING)
        pp->state = RUNNABLE;
    }
    release(&pp->lock);
  }

  // Phase 2: wait for and reap every other member. Bounded by them
  // noticing `killed` the next time they trap into the kernel (timer
  // interrupt, syscall, etc. — see usertrap()'s killed(p) check).
  // Reaping happens under join_lock, same lock/order thread_join()
  // uses, so we can never free a target out from under a joiner
  // that's mid-way through checking it.
  do {
    pending = 0;
    int reaped = 0;
    acquire(&join_lock);
    for (struct proc *pp = proc; pp < &proc[NPROC]; pp++) {
      if (pp == me)
        continue;
      acquire(&pp->lock);
      if (pp->tgid != tgid || pp->state == UNUSED) {
        release(&pp->lock);
        continue;
      }
      if (pp->state == ZOMBIE) {
        freeproc(pp);
        release(&pp->lock);
        reaped = 1;
      } else {
        pending = 1;
        release(&pp->lock);
      }
    }
    release(&join_lock);
    if (reaped)
      wakeup(&join_lock); // let any thread_join() sleeper re-check
    if (pending)
      yield();
  } while (pending);
}
