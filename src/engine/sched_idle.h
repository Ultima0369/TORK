#ifndef SCHED_IDLE_H
#define SCHED_IDLE_H

#include "scheduler.h"

void tick_idle(sched_ctx_t *ctx);
void tick_feedback(sched_ctx_t *ctx);

#endif
