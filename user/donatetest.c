// donatetest.c — Member 4: verifies priority donation + schedstat logging.
//
// A second mutex (pmid) serializes printf() calls across the two
// worker threads, so console output prints as clean, ordered lines
// instead of interleaving mid-word when both threads print at once.

#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"
#include "user/schedstat.h"

int mid;   // the mutex under test (donation target)
int pmid;  // print-serializing mutex — output clarity only, not part of the test
char low_stack[4096];
char high_stack[4096];
struct schedstat_rec dump_buf[64];

void
safe_print(char *msg)
{
  mutex_lock(pmid);
  printf("%s", msg);
  mutex_unlock(pmid);
}

void
burn_cpu(long n)
{
  for (volatile long i = 0; i < n; i++) {}
}

void
low_priority_worker(void *arg)
{
  safe_print("[low] burning CPU to force demotion...\n");
  burn_cpu(50000000);
  safe_print("[low] now locking mutex (should be demoted by now)\n");
  mutex_lock(mid);
  safe_print("[low] got the mutex, holding it for a LONG time...\n");
  burn_cpu(200000000);
  safe_print("[low] releasing the mutex\n");
  mutex_unlock(mid);
  thread_exit(0);
}

void
high_priority_worker(void *arg)
{
  burn_cpu(60000000);
  safe_print("[high] trying to lock (should block on low's held mutex)...\n");
  mutex_lock(mid);
  safe_print("[high] got the mutex!\n");
  mutex_unlock(mid);
  thread_exit(0);
}

int
main(int argc, char *argv[])
{
  mid = mutex_create();
  pmid = mutex_create();
  if (mid < 0 || pmid < 0) {
    printf("mutex_create failed\n");
    exit(1);
  }

  int t1 = thread_create(low_priority_worker, 0, low_stack + sizeof(low_stack) - 16);
  int t2 = thread_create(high_priority_worker, 0, high_stack + sizeof(high_stack) - 16);

  thread_join(t1);
  thread_join(t2);

  printf("\n--- schedstat dump (all records) ---\n");
  int n = getschedstat(dump_buf, 64);
  if (n < 0) {
    printf("getschedstat failed\n");
    exit(1);
  }
  for (int i = 0; i < n; i++) {
    printf("tick=%d tgid=%d tid=%d cpu=%d level=%d\n",
           dump_buf[i].tick, dump_buf[i].tgid, dump_buf[i].tid,
           dump_buf[i].cpu_id, dump_buf[i].priority);
  }

  exit(0);
}
