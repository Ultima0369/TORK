#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "soul_access.h"
#include "instinct.h"
#include "tln.h"
#include "../learning/pi_seed.h"

/* ── Scheduler context: everything periodic tasks need ── */
typedef struct {
    soul_t        *soul;
    int            round;          /* current loop iteration i */
    int            rounds_total;   /* total rounds requested (-1=forever) */
    int            quiet;
    int            drive;          /* computed drive value */

    instinct_input_t inp;
    tork_instinct_t  inst;

    /* tracked state */
    int            mod_attempted;
    int            opt_attempted;
    int            nop_attempted;
    int            rounds_since_mod;
    uint32_t       last_bb_tick;
    uint32_t       prev_bb_opt_at_idle;
    int            idle_discoveries;

    /* feedback */
    int            feedback_pending;
    int            feedback_round;

    /* drive bounds (int8_t range) */
#define DRIVE_MAX  127
#define DRIVE_MIN  (-128)

    /* TLN: 三进制逻辑推理网络 */
    TernaryNet tln;
    int tln_action_hint;
    int tln_modify_hint;
    int tln_explore_hint;
    int tln_energy_hint;
    int tln_enabled;           /* 0=关, 1=开 */
    int heartbeat_fastened;    /* P0-1: 心跳已加速标志 */
    int observe_cooldown;      /* P0-1: 观察超时后冷却 tick 数 */
    uint8_t        feedback_hw_before;
    int8_t         feedback_drive_before;

    /* staleness */
    uint8_t        prev_hw_stress;
    int8_t         prev_drive_val;
    int            env_stagnation_count;

    /* rhythm */
    rhythm_tracker_t rhythm;
    int            rhythm_inited;

    /* golden restore observation */
    int            golden_observe_remaining;
} sched_ctx_t;

void scheduler_init(sched_ctx_t *ctx, soul_t *soul, int quiet);
void scheduler_tick(sched_ctx_t *ctx);

#endif
