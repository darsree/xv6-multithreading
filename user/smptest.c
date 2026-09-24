// smptest.c — dedicated SMP test.
//
// Proves threads run on several CPUs AT THE SAME TIME, and shows how
// the load is spread across CPUs.
//
//   Phase 1: 1 CPU-bound thread              -> time T1
//   Phase 2: 3 CPU-bound threads (same work each) -> time T3
//            If they really run in parallel, T3 ~= T1 (not 3 x T1).
//   Phase 3: 6 threads on 3 CPUs (oversubscribed) -> time T6 ~= 2 x T1,
//            and the load balancer spreads them over all CPUs.
//
// speedup = threads * T1 / Tn. Ideal = min(threads, CPUs).
// After each phase: dispatches per CPU for each thread (getschedstat)
// and how often it changed CPU. Kernel "smp_balance: migrating ..."
// lines are the live balancer events.
//
// (The spin length is auto-calibrated so each 1-thread run takes >= 15
// ticks; runs shorter than that are reported INVALID, not PASS.)
//
// Usage (needs make qemu with CPUS >= 2, default is 3):
//   $ smptest

#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"
#include "user/schedstat.h"

#define MAXT  6
long work;   // spin iterations per thread, set by calibrate()

char t_stack[MAXT][4096];
struct schedstat_rec sbuf[SCHEDSTAT_RINGSIZE];

// ---- line-buffered output: build a line, flush with ONE write() ----
static char lb[200];
static int llen;

static void lc(char c) { if (llen < (int)sizeof(lb) - 1) lb[llen++] = c; }
static void ls(const char *s) { while (*s) lc(*s++); }

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

static void
lcpu(int c, int w)
{
  for (int p = 4; p < w; p++)
    lc(' ');
  ls("cpu");
  lc('0' + c);
}

// x100 fixed point, e.g. 272 -> "2.72x", right-aligned in width w
static void
lfix(int x100, int w)
{
  char t[16];
  int k = 0;
  t[k++] = 'x';
  t[k++] = '0' + x100 % 10;
  t[k++] = '0' + (x100 / 10) % 10;
  t[k++] = '.';
  int ip = x100 / 100;
  do {
    t[k++] = '0' + ip % 10;
    ip /= 10;
  } while (ip);
  for (int p = k; p < w; p++)
    lc(' ');
  while (k)
    lc(t[--k]);
}

static void
lend(void)
{
  lc('\n');
  write(1, lb, llen);
  llen = 0;
}

static void
rule(int n)
{
  ls("  ");
  for (int i = 0; i < n; i++)
    lc('-');
  lend();
}

void
worker(void *arg)
{
  for (volatile long i = 0; i < work; i++) {}
  thread_exit(0);
}

// Find a spin count that takes >= CAL_TICKS ticks on ONE thread, so the
// timings below are long enough to mean something (1 tick ~ 100 ms).
#define CAL_TICKS 15

static long
calibrate(void)
{
  long w = 10000000;
  for (;;) {
    int t0 = uptime();
    for (volatile long i = 0; i < w; i++) {}
    if (uptime() - t0 >= CAL_TICKS)
      return w;
    w *= 4;
  }
}

// run nt workers to completion, return elapsed ticks
static int
run_phase(int nt, int *tids)
{
  int t0 = uptime();
  for (int i = 0; i < nt; i++)
    tids[i] = thread_create(worker, 0, t_stack[i] + sizeof(t_stack[i]) - 16);
  for (int i = 0; i < nt; i++)
    thread_join(tids[i]);
  int dt = uptime() - t0;
  return dt > 0 ? dt : 1;
}

#define S_TID 5
#define S_CPU 7
#define S_TOT 8
#define S_CHG 13

static void
smp_report(int *tids, int nt)
{
  int disp[MAXT][NCPU];
  int mig[MAXT], last[MAXT];
  int maxcpu = 0;
  int n = getschedstat(sbuf, SCHEDSTAT_RINGSIZE);

  if (n < 0) {
    ls("smp balance: getschedstat failed");
    lend();
    return;
  }
  for (int t = 0; t < nt; t++) {
    mig[t] = 0;
    last[t] = -1;
    for (int c = 0; c < NCPU; c++)
      disp[t][c] = 0;
  }
  for (int i = 0; i < n; i++) {
    struct schedstat_rec *r = &sbuf[i];
    if (r->cpu_id < 0 || r->cpu_id >= NCPU)
      continue;
    for (int t = 0; t < nt; t++) {
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

  ls("dispatches per CPU (from getschedstat):");
  lend();
  ls("  ");
  lsp("tid", S_TID);
  for (int c = 0; c <= maxcpu; c++)
    lcpu(c, S_CPU);
  lsp("total", S_TOT);
  lsp("cpu-changes", S_CHG);
  lend();
  rule(S_TID + S_CPU * (maxcpu + 1) + S_TOT + S_CHG);

  for (int t = 0; t < nt; t++) {
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
  rule(S_TID + S_CPU * (maxcpu + 1) + S_TOT + S_CHG);
  ls("  ");
  lsp("all", S_TID);
  for (int c = 0; c <= maxcpu; c++) {
    int s = 0;
    for (int t = 0; t < nt; t++)
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

int
main(int argc, char *argv[])
{
  int tids[MAXT];
  int T[3];
  int nts[3] = {1, 3, 6};
  const char *names[3] = {"1 thread", "3 threads (parallel)",
                          "6 threads (oversubscribed)"};

  ls("smptest: calibrating spin length...");
  lend();
  work = calibrate();
  ls("smptest: each thread spins ");
  lnum((int)(work / 1000000), 0);
  ls(" million iterations");
  lend();

  for (int p = 0; p < 3; p++) {
    banner(names[p]);
    T[p] = run_phase(nts[p], tids);
    ls("elapsed: ");
    lnum(T[p], 0);
    ls(" ticks");
    lend();
    smp_report(tids, nts[p]);
  }

  banner("SUMMARY");
  ls("  ");
  lsp("phase", 28);
  lsp("threads", 9);
  lsp("ticks", 8);
  lsp("speedup", 10);
  lend();
  rule(28 + 9 + 8 + 10);
  for (int p = 0; p < 3; p++) {
    ls("  ");
    lsp(names[p], 28);
    lnum(nts[p], 9);
    lnum(T[p], 8);
    lfix(nts[p] * T[0] * 100 / T[p], 10);
    lend();
  }

  int sp3 = nts[1] * T[0] * 100 / T[1];
  ls("");
  lend();
  if (T[0] < 10)
    ls("-> INVALID: runs too short to time -- increase CAL_TICKS");
  else if (sp3 >= 200)
    ls("-> PASS: 3 threads did 3x the work in about the same time -- "
       "they ran on different CPUs simultaneously");
  else if (sp3 >= 130)
    ls("-> PARTIAL: some parallelism, but less than expected -- "
       "rerun or check nothing else is running");
  else
    ls("-> FAIL: no real parallelism -- check QEMU -smp / CPUS");
  lend();

  exit(0);
}