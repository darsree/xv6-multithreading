// threadexectest.c — regression test for the thread+exec integration
// fix in kernel/exec.c.
//
// Before the fix: kexec() always freed the calling proc's OLD
// pagetable unconditionally, even when it was shared (mirrored) with
// other threads in the same tgid. If the leader (or any thread)
// called exec() while a sibling thread was still alive, that freed
// physical pages the sibling was still actively mapping/executing
// out of -- a use-after-free that reliably corrupts memory or
// crashes.
//
// This test creates one sibling thread that spins in a tight loop
// (so it's continuously fetching instructions out of the shared code
// pages), then has the leader exec() into "echo" while the sibling
// is still mid-loop.
//
// Usage (inside the xv6 shell):
//   $ threadexectest
//
// Expected (PASS): the sibling's "still alive" line, then this
// process becomes "echo" and prints its argv, then control returns
// cleanly to the shell. Any panic, hang, or garbled/missing output
// is a FAIL -- rerun a few times, since UAF timing can vary.

#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

char sibling_stack[4096];
volatile int sibling_started = 0;

void
sibling(void *arg)
{
  sibling_started = 1;
  // Keep actively executing code out of the shared (mirrored) text
  // pages for a while, so it's genuinely using them while the leader
  // exec()s out from under the group. If the fix regresses, this is
  // what will crash or corrupt.
  for (volatile long i = 0; i < 200000000; i++) {}
  printf("threadexectest: sibling finished normally\n");
  thread_exit(0);
}

int
main(int argc, char *argv[])
{
  if (thread_create(sibling, 0, sibling_stack + sizeof(sibling_stack) - 16) <
      0) {
    printf("threadexectest: FAIL -- thread_create failed\n");
    exit(1);
  }

  // Don't exec until the sibling is actually running.
  while (!sibling_started) {}

  printf("threadexectest: leader exec()ing now, sibling still running\n");

  char *argv2[] = {"echo", "threadexectest:", "exec-survived", "PASS", 0};
  exec("echo", argv2);

  // Only reached if exec() itself failed to start.
  printf("threadexectest: FAIL -- exec() returned\n");
  exit(1);
}