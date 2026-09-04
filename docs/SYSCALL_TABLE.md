# Reserved syscall number block: 22–35

| # | Name | Owner |
|---|---|---|
| 22 | thread_create | M1 |
| 23 | thread_join | M1 |
| 24 | thread_exit | M1 |
| 25 | mutex_create | M2 |
| 26 | mutex_lock | M2 |
| 27 | mutex_unlock | M2 |
| 28 | cv_wait | M2 |
| 29 | cv_signal / cv_broadcast | M2 |
| 30–31 | (reserved, M3 — likely unused, scheduler has no new syscalls) | M3 |
| 32 | getschedstat | M4 |
| 33–35 | (reserved buffer) | — |

Rule: each member appends ONLY their own rows to syscall.h /
sysproc.c / usys.pl / user/user.h. Never renumber or reflow existing
entries.
