#include "sched_services.h"
#include "sched_tln.h"
#include "soul_access.h"
#include "../grid/grid_soul_connector.h"
#include "torkd.h"
#include "task.h"
#include "../learning/distributed.h"
#include "../learning/mentor.h"
#include "../learning/experience.h"
#include "../learning/branch.h"
#include <stdio.h>
#include <string.h>

/* ── Per-tick services ── */
void tick_services(sched_ctx_t *ctx) {
    instinct_input_t *inp = &ctx->inp;

    /* grid: push soul data */
    {
        grid_soul_feed_t gs;
        memset(&gs, 0, sizeof(gs));
        gs.tick = inp->tick;
        gs.drive = (int8_t)ctx->drive;
        gs.hw_stress = inp->hw_stress;
        gs.active_branches = br_active_count();
        gs.experience_count = exp_count();
        gs.energy_mode = 1;
        gs.fear = (uint8_t)(ctx->inst.fear * 100);
        gs.desire = (uint8_t)(ctx->inst.desire * 100);
        gs.curiosity = (uint8_t)(ctx->inst.curiosity * 100);
        experience_t recent[16];
        int n = exp_recent(16, recent);
        gs.outcome_count = 0;
        for (int ei = 0; ei < n && ei < 16; ei++) {
            gs.recent_outcomes[ei] = recent[ei].outcome;
            gs.outcome_count++;
        }
        grid_engine_write(&gs);
    }

    torkd_tick();
    task_process_one();
    dist_tick();

    /* 师徒阶段更新 (每 100 tick) */
    if (ctx->round % 100 == 0) {
        float pat_conf = ctx->inp.pattern_confidence;
        /* TLN 一致性: 最近4个hint中非零的比例 */
        int tln_nonzero = 0;
        if (ctx->tln_action_hint != 0) tln_nonzero++;
        if (ctx->tln_modify_hint != 0) tln_nonzero++;
        if (ctx->tln_explore_hint != 0) tln_nonzero++;
        if (ctx->tln_energy_hint != 0) tln_nonzero++;
        float tln_cons = (float)tln_nonzero / 4.0f;
        mentor_tick(exp_count(), pat_conf, tln_cons);
    }

    /* TLN: 每 tick 做一步三值逻辑推理 */
    tick_services_tln(ctx);
}
