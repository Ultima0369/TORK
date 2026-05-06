#ifndef SCHED_INDUCTIVE_H
#define SCHED_INDUCTIVE_H

#include "scheduler.h"

void tick_inductive(sched_ctx_t *ctx);
void tick_inductive_test(sched_ctx_t *ctx);
void tick_inductive_apply(sched_ctx_t *ctx);

#endif
