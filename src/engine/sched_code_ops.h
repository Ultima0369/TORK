#ifndef SCHED_CODE_OPS_H
#define SCHED_CODE_OPS_H

#include "scheduler.h"

void tick_code_read(sched_ctx_t *ctx);
void tick_code_modify(sched_ctx_t *ctx);
void tick_code_optimize(sched_ctx_t *ctx);
void tick_nop_delete(sched_ctx_t *ctx);

#endif
