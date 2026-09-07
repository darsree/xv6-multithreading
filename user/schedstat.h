// schedstat.h — user-space mirror of kernel/schedstat.h's record layout
#ifndef USER_SCHEDSTAT_H
#define USER_SCHEDSTAT_H

struct schedstat_rec {
  unsigned int tick;
  int   tgid;
  int   tid;
  int   is_thread;
  int   state;
  int   cpu_id;
  int   priority;
};

#define SCHEDSTAT_RINGSIZE 1024

#endif