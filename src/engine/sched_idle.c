#include "sched_idle.h"
#include "soul_access.h"
#include "idler.h"
#include "../learning/mcts.h"
#include "blackboard.h"
#include "../learning/experience.h"

/* ── Idle check (every 100) ── */
void tick_idle(sched_ctx_t *ctx) {
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    idler_input_t idle_in = {
        .hw_stress       = inp->hw_stress,
        .drive           = drive,
        .gen_count       = soul_gen_count(soul),
        .tick            = inp->tick,
        .last_bb_tick    = ctx->last_bb_tick,
        .rounds_since_mod = ctx->rounds_since_mod,
    };

    if (idler_should_enter(&idle_in)) {
        if (!idler_active()) {
            printf("[%4d] tick=%-6u IDLE: entering idle (stress=%d drive=%d gen=%u)\n",
                   i, inp->tick, inp->hw_stress, drive, soul_gen_count(soul));
            uint8_t mode_idle = 1;
            soul_write_buf(soul, S_MODE, &mode_idle, 1);
            idler_set_active(1);
            idler_output_t idle_out = idler_cycle(&idle_in);
            int disc = idle_out.discoveries;
            if (disc > 0) ctx->idle_discoveries += disc;
            else if (disc == 0) ctx->idle_discoveries = IDLE_ENDED_NONE;
            printf("[%4d] tick=%-6u IDLE: exiting idle (action=%d, discoveries=%d)\n",
                   i, inp->tick, idle_out.action_type, disc);

            if (idle_out.action_type >= MCTS_MOD_REPLACE_OP &&
                idle_out.action_type <= MCTS_MOD_SWAP_REGS) {
                int mod_rc = idler_mcts_modify(idle_out.action_type,
                                               "benchmark/memcpy/ref.s", "memcpy_tork");
                if (mod_rc == 0) {
                    exp_update_last(60, 0, 1, inp->hw_stress, drive);
                    bb_write(BB_TYPE_OPT_SUCCESS, idle_out.action_type,
                             (uint32_t)idle_out.action_type);
                    bb_inc_optimizations();
                } else {
                    exp_update_last(-20, 1, 0, inp->hw_stress, drive);
                    {
                        int16_t worst = soul_worst_outcome(soul);
                        if (-20 < worst) {
                            int16_t new_worst = -20;
                            soul_write_buf(soul, S_WORST_OUTCOME, &new_worst, 2);
                        }
                    }
                }
            }

            /* IDLE 无发现 = 无行动失败 */
            if (disc <= 0) {
                int16_t worst = soul_worst_outcome(soul);
                if (0 > worst) {
                    int16_t new_worst = 0;
                    soul_write_buf(soul, S_WORST_OUTCOME, &new_worst, 2);
                }
            }

            uint8_t mode_busy = 0;
            soul_write_buf(soul, S_MODE, &mode_busy, 1);
            ctx->feedback_pending = 1;
            ctx->feedback_round = i;
            ctx->feedback_hw_before = inp->hw_stress;
            ctx->prev_bb_opt_at_idle = bb_global_optimizations();
            ctx->feedback_drive_before = (int8_t)drive;
            idler_set_active(0);
        }
    } else if (idler_active()) {
        printf("[%4d] tick=%-6u IDLE: conditions changed, forced exit\n", i, inp->tick);
        uint8_t mode_busy = 0;
        soul_write_buf(soul, S_MODE, &mode_busy, 1);
        idler_set_active(0);
    }
}

/* ── Feedback evaluation ── */
void tick_feedback(sched_ctx_t *ctx) {
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    if (!ctx->feedback_pending || i - ctx->feedback_round < 50)
        return;

    int8_t outcome = 0;
    uint8_t hw_change = (inp->hw_stress < ctx->feedback_hw_before) ? 1 :
                        (inp->hw_stress > ctx->feedback_hw_before) ? 2 : 0;
    int8_t drive_change = (int8_t)drive - ctx->feedback_drive_before;

    if (hw_change == 1) outcome += 20;
    else if (hw_change == 2) outcome -= 15;
    if (drive_change > 10) outcome += 30;
    else if (drive_change > 0) outcome += 10;
    else if (drive_change < -10) outcome -= 20;
    else if (drive_change < 0) outcome -= 5;

    uint32_t opt_delta = bb_global_optimizations() - ctx->prev_bb_opt_at_idle;
    if (opt_delta > 0) outcome += 25;

    exp_update_last(outcome, 0, 0, inp->hw_stress, drive);
    printf("[%4d] tick=%-6u FB: outcome=%d (hw=%d→%d, drive=%d→%d, opts=%u)\n",
           i, inp->tick, outcome, ctx->feedback_hw_before, inp->hw_stress,
           ctx->feedback_drive_before, drive, opt_delta);
    ctx->feedback_pending = 0;
}
