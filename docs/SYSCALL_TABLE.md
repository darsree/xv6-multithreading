# Reserved syscall number block: 22–35

**UPDATE (M1, pre-Week-1-freeze):** the base repo already had
`SYS_sync = 22` (from `user/_sync`, predates this reservation). That
collided head-on with `SYS_thread_create`, which was also written as
22. Fixed by shifting the whole M1/M2 block up by 1. `SYS_getschedstat`
is untouched — the only effect is the reserved-but-unused gap moves
from 30 to 31. **M2: your `mutex_*`/`cv_*` numbers below changed —
update anything you already hardcoded against the old table.**

| # | Name | Owner |
|---|---|---|
| 22 | sync (pre-existing, not part of this project) | — |
| 23 | thread_create | M1 |
| 24 | thread_join | M1 |
| 25 | thread_exit | M1 |
| 26 | mutex_create | M2 |
| 27 | mutex_lock | M2 |
| 28 | mutex_unlock | M2 |
| 29 | cv_wait | M2 |
| 30 | cv_signal / cv_broadcast | M2 |
| 31 | (reserved, unused) | M3 |
| 32 | getschedstat | M4 |
| 33–35 | (reserved buffer) | — |

Rule: each member appends ONLY their own rows to syscall.h /
sysproc.c / usys.pl / user/user.h. Never renumber or reflow existing
entries.
