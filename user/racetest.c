// racetest.c — shared-memory race demo + intermediate values + SMP balance.
//
// Part 1: NTHREADS threads increment one global with NO lock.
// Part 2: same loop under a kmutex.
//
// For each part it prints
//   (a) INTERMEDIATE VALUES — every SNAP_EVERY increments each thread
//       records (increments done by all threads so far, counter value
//       it sees). Without a mutex "counter" falls behind "done" (lost
//       updates building up). With a mutex they match every time.
//   (b) SMP BALANCE — from getschedstat(): dispatches per CPU for each
//       thread, and how often it changed CPU.
//
// Output is assembled one line at a time and sent with a single write(),
// so kernel "mlfq:" / "smp_balance:" log lines can only land BETWEEN
// lines, not in the middle of a table row.
//
// Usage: $ racetest

#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"
#include "user/schedstat.h"

#define NTHREADS   4
#define ITERS      20000
#define SNAP_EVERY 4000
#define NSNAP      (ITERS / SNAP_EVERY)

volatile int unsafe_counter = 0;
int safe_counter = 0;
int cmid;

volatile int progress[NTHREADS];   // each thread writes only its own slot

struct snap { int total; int seen; };
struct snap snaps[NTHREADS][NSNAP];

struct schedstat_rec sbuf[SCHEDSTAT_RINGSIZE];

char t_stack[NTHREADS][4096];

// ---- line-buffered output: build a line, flush with ONE write() ----
static char lb[200];
static int llen;

static void
lc(char c)
{
  if (llen < (int)sizeof(lb) - 1)
    lb[llen++] = c;
}

static void
ls(const char *s)
{
  while (*s)
    lc(*s++);
}

// string, right-aligned in a field of width w
static void
lsp(const char *s, int w)
{
  int l = 0;
  while (s[l])
    l++;
  for (; l < w; l++)
    lc(' ');
  ls(s);
}

// number, right-aligned in a field of width w
static void
lnum(int n, int w)
{
  char t[12];
  int k = 0;
  int neg = n < 0;
  unsigned x = neg ? (unsigned)(-n) : (unsigned)n;
  do {
    t[k++] = '0' + x % 10;
    x /= 10;
  } while (x);
  if (neg)
    t[k++] = '-';
  for (int p = k; p < w; p++)
    lc(' ');
  while (k)
    lc(t[--k]);
}

// "cpuN" right-aligned in a field of width w
static void
lcpu(int c, int w)
{
  for (int p = 4; p < w; p++)
    lc(' ');
  ls("cpu");
  lc('0' + c);
}

static void
lend(void)
{
  lc('\n');
  write(1, lb, llen);
  llen = 0;
}

static int
total_done(void)
{
  int s = 0;
  for (int t = 0; t < NTHREADS; t++)
    s += progress[t];
  return s;
}

void
unsafe_worker(void *arg)
{
  int idx = (int)(long)arg;
  for (int i = 0; i < ITERS; i++) {
    int tmp = unsafe_counter;   // read
    for (volatile int d = 0; d < 500; d++) {}
    tmp = tmp + 1;              // modify
    unsafe_counter = tmp;       // write -- others can sneak in between
    progress[idx] = i + 1;
    if ((i + 1) % SNAP_EVERY == 0) {
      struct snap *s = &snaps[idx][(i + 1) / SNAP_EVERY - 1];
      s->total = total_done();
      s->seen = unsafe_counter;
    }
  }
  thread_exit(0);
}

void
safe_worker(void *arg)
{
  int idx = (int)(long)arg;
  for (int i = 0; i < ITERS; i++) {
    mutex_lock(cmid);
    safe_counter++;
    progress[idx] = i + 1;
    if ((i + 1) % SNAP_EVERY == 0) {   // taken inside the lock: exact
      struct snap *s = &snaps[idx][(i + 1) / SNAP_EVERY - 1];
      s->total = total_done();
      s->seen = safe_counter;
    }
    mutex_unlock(cmid);
  }
  thread_exit(0);
}

// column widths for the intermediate-values table
#define W_THR  6
#define W_ITER 8
#define W_DONE 13
#define W_SEEN 13
#define W_LOST 13

static void
print_snaps(void)
{
  ls("intermediate values (checkpoint every ");
  lnum(SNAP_EVERY, 0);
  ls(" increments per thread):");
  lend();

  ls("  ");
  lsp("thread", W_THR);
  lsp("iter", W_ITER);
  lsp("done-so-far", W_DONE);
  lsp("counter-seen", W_SEEN);
  lsp("lost-so-far", W_LOST);
  lend();

  ls("  ");
  for (int i = 0; i < W_THR + W_ITER + W_DONE + W_SEEN + W_LOST; i++)
    lc('-');
  lend();

  for (int t = 0; t < NTHREADS; t++) {
    for (int k = 0; k < NSNAP; k++) {
      ls("  ");
      lnum(t, W_THR);
      lnum((k + 1) * SNAP_EVERY, W_ITER);
      lnum(snaps[t][k].total, W_DONE);
      lnum(snaps[t][k].seen, W_SEEN);
      lnum(snaps[t][k].total - snaps[t][k].seen, W_LOST);
      lend();
    }
  }
}

// column widths for the SMP table
#define S_TID   5
#define S_CPU   7
#define S_TOT   8
#define S_CHG   13

static void
smp_report(int *tids)
{
  int disp[NTHREADS][NCPU];
  int mig[NTHREADS], last[NTHREADS];
  int maxcpu = 0;
  int n = getschedstat(sbuf, SCHEDSTAT_RINGSIZE);

  if (n < 0) {
    ls("smp balance: getschedstat failed");
    lend();
    return;
  }
  for (int t = 0; t < NTHREADS; t++) {
    mig[t] = 0;
    last[t] = -1;
    for (int c = 0; c < NCPU; c++)
      disp[t][c] = 0;
  }
  for (int i = 0; i < n; i++) {
    struct schedstat_rec *r = &sbuf[i];
    if (r->cpu_id < 0 || r->cpu_id >= NCPU)
      continue;
    for (int t = 0; t < NTHREADS; t++) {
      if (r->tid != tids[t])
        continue;
      disp[t][r->cpu_id]++;
      if (last[t] >= 0 && last[t] != r->cpu_id)
        mig[t]++;
      last[t] = r->cpu_id;
      if (r->cpu_id > maxcpu)
        maxcpu = r->cpu_id;
    }
  }

  ls("smp balance (dispatches per CPU, from getschedstat):");
  lend();

  ls("  ");
  lsp("tid", S_TID);
  for (int c = 0; c <= maxcpu; c++)
    lcpu(c, S_CPU);
  lsp("total", S_TOT);
  lsp("cpu-changes", S_CHG);
  lend();

  ls("  ");
  for (int i = 0; i < S_TID + S_CPU * (maxcpu + 1) + S_TOT + S_CHG; i++)
    lc('-');
  lend();

  for (int t = 0; t < NTHREADS; t++) {
    int tot = 0;
    ls("  ");
    lnum(tids[t], S_TID);
    for (int c = 0; c <= maxcpu; c++) {
      lnum(disp[t][c], S_CPU);
      tot += disp[t][c];
    }
    lnum(tot, S_TOT);
    lnum(mig[t], S_CHG);
    lend();
  }

  int grand = 0;
  ls("  ");
  lsp("all", S_TID);
  for (int c = 0; c <= maxcpu; c++) {
    int s = 0;
    for (int t = 0; t < NTHREADS; t++)
      s += disp[t][c];
    lnum(s, S_CPU);
    grand += s;
  }
  lnum(grand, S_TOT);
  lend();

  if (n == SCHEDSTAT_RINGSIZE) {
    ls("  WARNING: ring buffer full, oldest records overwritten");
    lend();
  }
}

static void
banner(const char *s)
{
  ls("");
  lend();
  ls("=== ");
  ls(s);
  ls(" ===");
  lend();
}

static void
result(const char *name, int actual, int expected)
{
  ls("expected: ");
  lnum(expected, 0);
  lend();
  ls(name);
  lnum(actual, 0);
  lend();
}

int
main(int argc, char *argv[])
{
  int tid[NTHREADS];
  int i;
  int expected = NTHREADS * ITERS;

  ls("racetest: ");
  lnum(NTHREADS, 0);
  ls(" threads x ");
  lnum(ITERS, 0);
  ls(" increments each, expecting ");
  lnum(expected, 0);
  lend();

  banner("WITHOUT a mutex");
  for (i = 0; i < NTHREADS; i++)
    tid[i] = thread_create(unsafe_worker, (void *)(long)i,
                           t_stack[i] + sizeof(t_stack[i]) - 16);
  for (i = 0; i < NTHREADS; i++)
    thread_join(tid[i]);

  print_snaps();
  result("actual:   ", unsafe_counter, expected);
  if (unsafe_counter != expected) {
    ls("-> LOST ");
    lnum(expected - unsafe_counter, 0);
    ls(" increments to a race condition (real shared memory + real concurrency)");
  } else {
    ls("-> no lost updates this run -- timing-dependent, run again");
  }
  lend();
  ls("");
  lend();
  smp_report(tid);

  for (i = 0; i < NTHREADS; i++)
    progress[i] = 0;
  cmid = mutex_create();

  banner("WITH a mutex, same increment pattern");
  for (i = 0; i < NTHREADS; i++)
    tid[i] = thread_create(safe_worker, (void *)(long)i,
                           t_stack[i] + sizeof(t_stack[i]) - 16);
  for (i = 0; i < NTHREADS; i++)
    thread_join(tid[i]);

  print_snaps();
  result("actual:   ", safe_counter, expected);
  ls(safe_counter == expected
         ? "-> PASS: exact match, mutex prevented every lost update"
         : "-> FAIL: mismatch even with mutex -- check kmutex impl");
  lend();
  ls("");
  lend();
  smp_report(tid);

  exit(0);
}