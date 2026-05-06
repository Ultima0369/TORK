#ifndef SCHED_MONITOR_H
#define SCHED_MONITOR_H

#include "scheduler.h"

void tick_monitoring(sched_ctx_t *ctx);
void tick_observer_energy(sched_ctx_t *ctx);
void tick_snapshot(sched_ctx_t *ctx);
void tick_pi_rhythm(sched_ctx_t *ctx);

#endif
