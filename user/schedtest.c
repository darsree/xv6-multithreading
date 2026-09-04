// schedtest.c — Member 3 deliverable
// Mix of a CPU-bound spin-loop process and a sleep/wake-heavy process.
// Log queue level over time; show CPU-bound demotes/settles while
// I/O-bound stays responsive; show starvation/promotion for a
// long-waiting low-priority process.
#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  printf("schedtest: TODO implement CPU-bound vs I/O-bound mix test\n");
  exit(0);
}
