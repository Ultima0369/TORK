#ifndef FISSION_H
#define FISSION_H

#include <sys/types.h>
#include "soul_access.h"

/* Should we fission? Returns 1=yes, 0=no */
int fission_decide(const soul_t *soul);

/* Execute fission: fork a child instance in its own directory.
   Returns child PID on success, -1 on failure. */
pid_t fission_spawn(void);

/* Collect child state for timeout_ticks intervals (~0.5s each).
   Returns: 0=child better, 1=parent better, -1=child dead */
int fission_collect(pid_t child_pid, int timeout_ticks);

/* Sovereignty migration: terminate the loser, keep the winner.
   Returns: 0=parent won (child terminated), 1=child won (parent exits) */
int fission_migrate(pid_t child_pid);

/* Clean up lock file (call on exit) */
void fission_cleanup(void);

#endif
