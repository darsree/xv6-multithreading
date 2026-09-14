#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#include <stdarg.h>

static char digits[] = "0123456789ABCDEF";

// Root-cause fix for console interleaving: stock xv6's putc() used to
// call write(fd, &c, 1) -- one syscall PER CHARACTER. That gave any
// concurrent writer on another CPU (kernel printk(), which does hold
// a lock for its whole line) endless chances to interleave into the
// middle of a single printf()/fprintf() call, byte by byte. This is
// what was splicing kernel "mlfq:"/"donate:" debug lines into the
// middle of schedstat_dump's CSV rows.
//
// Buffering each vprintf() call and flushing it with ONE write() per
// call (or per PBUFSZ-sized chunk, for output longer than that) closes
// almost all of that window: the whole line leaves in a single syscall
// instead of one per digit. It doesn't add a new lock -- printk()
// still isn't synchronized with consolewrite() -- but it shrinks the
// number of syscall boundaries a foreign writer could land in from
// "one per character" to "at most one per print call", which is why
// this alone is enough to stop rows from being split in practice. A
// fully airtight fix would need printk() and consolewrite() to share
// a lock across the whole write; this is the safe, low-risk version
// of that fix.
#define PBUFSZ 128

struct pbuf {
  int fd;
  char buf[PBUFSZ];
  int len;
};

static void
pflush(struct pbuf *pb)
{
  if (pb->len > 0)
    write(pb->fd, pb->buf, pb->len);
  pb->len = 0;
}

static void
putc(struct pbuf *pb, char c)
{
  if (pb->len == PBUFSZ)
    pflush(pb);
  pb->buf[pb->len++] = c;
}

static void
printint(struct pbuf *pb, long long xx, int base, int sgn)
{
  char buf[20];
  int i, neg;
  unsigned long long x;

  neg = 0;
  if (sgn && xx < 0) {
    neg = 1;
    x = -xx;
  } else {
    x = xx;
  }

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while ((x /= base) != 0);
  if (neg)
    buf[i++] = '-';

  while (--i >= 0)
    putc(pb, buf[i]);
}

static void
printptr(struct pbuf *pb, uint64 x)
{
  int i;
  putc(pb, '0');
  putc(pb, 'x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    putc(pb, digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the given fd. Only understands %d, %x, %p, %c, %s.
void
vprintf(int fd, const char *fmt, va_list ap)
{
  char *s;
  int c0, c1, c2, i, state;
  struct pbuf pb;

  pb.fd = fd;
  pb.len = 0;

  state = 0;
  for (i = 0; fmt[i]; i++) {
    c0 = fmt[i] & 0xff;
    if (state == 0) {
      if (c0 == '%') {
        state = '%';
      } else {
        putc(&pb, c0);
      }
    } else if (state == '%') {
      c1 = c2 = 0;
      if (c0)
        c1 = fmt[i + 1] & 0xff;
      if (c1)
        c2 = fmt[i + 2] & 0xff;
      if (c0 == 'd') {
        printint(&pb, va_arg(ap, int), 10, 1);
      } else if (c0 == 'l' && c1 == 'd') {
        printint(&pb, va_arg(ap, uint64), 10, 1);
        i += 1;
      } else if (c0 == 'l' && c1 == 'l' && c2 == 'd') {
        printint(&pb, va_arg(ap, uint64), 10, 1);
        i += 2;
      } else if (c0 == 'u') {
        printint(&pb, va_arg(ap, uint32), 10, 0);
      } else if (c0 == 'l' && c1 == 'u') {
        printint(&pb, va_arg(ap, uint64), 10, 0);
        i += 1;
      } else if (c0 == 'l' && c1 == 'l' && c2 == 'u') {
        printint(&pb, va_arg(ap, uint64), 10, 0);
        i += 2;
      } else if (c0 == 'x') {
        printint(&pb, va_arg(ap, uint32), 16, 0);
      } else if (c0 == 'l' && c1 == 'x') {
        printint(&pb, va_arg(ap, uint64), 16, 0);
        i += 1;
      } else if (c0 == 'l' && c1 == 'l' && c2 == 'x') {
        printint(&pb, va_arg(ap, uint64), 16, 0);
        i += 2;
      } else if (c0 == 'p') {
        printptr(&pb, va_arg(ap, uint64));
      } else if (c0 == 'c') {
        putc(&pb, va_arg(ap, uint32));
      } else if (c0 == 's') {
        if ((s = va_arg(ap, char *)) == 0)
          s = "(null)";
        for (; *s; s++)
          putc(&pb, *s);
      } else if (c0 == '%') {
        putc(&pb, '%');
      } else {
        // Unknown % sequence.  Print it to draw attention.
        putc(&pb, '%');
        putc(&pb, c0);
      }

      state = 0;
    }
  }

  pflush(&pb);
}

void
fprintf(int fd, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vprintf(fd, fmt, ap);
}

void
printf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vprintf(1, fmt, ap);
}