#include "sched_tln.h"
#include "soul_access.h"
#include "../learning/self_tune.h"
#include "../learning/mcts.h"
#include "../learning/energy.h"
#include "../learning/snapshot.h"
#include <stdio.h>
#include <string.h>

/* TLN block extracted from tick_services (original lines 115-222) */
void tick_services_tln(sched_ctx_t *ctx) {
    instinct_input_t *inp = &ctx->inp;

    if (ctx->tln_enabled) {
        tln_val_t tln_in[TLN_INPUTS];
        tln_val_t tln_out[TLN_OUTPUTS];
        tln_val_t pat[4] = {
            (tln_val_t)inp->pattern_best_action,
            (tln_val_t)(inp->pattern_confidence > 0.3f ? 1 : 0),
            (tln_val_t)(inp->pattern_confidence > 0.6f ? 1 : 0),
            0
        };
        tln_encode_soul(ctx->soul->buf,
                         inp->hw_stress, (int8_t)ctx->drive,
                         soul_gen_count(ctx->soul), pat, tln_in);
        tln_step(&ctx->tln, tln_in, tln_out);
        tln_decode_output(tln_out,
                          &ctx->tln_action_hint,
                          &ctx->tln_modify_hint,
                          &ctx->tln_explore_hint,
                          &ctx->tln_energy_hint);
        /* TLN hints 写入 Soul，供 torkd status 读取 */
        int8_t tln_hints[4] = {
            (int8_t)ctx->tln_action_hint,
            (int8_t)ctx->tln_modify_hint,
            (int8_t)ctx->tln_explore_hint,
            (int8_t)ctx->tln_energy_hint
        };
        soul_write_buf(ctx->soul, S_TLN_ACTION, tln_hints, 4);

        /* TLN 主见 → 调制 self_tune 参数 (每 20 tick 一次，避免抖动) */
        if (ctx->round % 20 == 0) {
            tune_apply_tln_hints(ctx->tln_action_hint,
                                 ctx->tln_modify_hint,
                                 ctx->tln_explore_hint,
                                 ctx->tln_energy_hint);
        }

        /* TLN energy_hint → 能耗模式切换 (每 100 tick 一次) */
        if (ctx->round % 100 == 0) {
            if (ctx->tln_energy_hint == 1)
                eng_set_mode(ENERGY_MODE_PERFORMANCE);
            else if (ctx->tln_energy_hint == -1)
                eng_set_mode(ENERGY_MODE_ECONOMY);
            else
                eng_set_mode(ENERGY_MODE_BALANCED);
        }

        /* TLN explore_hint → MCTS 探索常数 (每 50 tick 一次) */
        if (ctx->round % 50 == 0) {
            float cur_c = mcts_get_exploration();
            if (ctx->tln_explore_hint == 1)
                mcts_set_exploration(cur_c + 0.05f);
            else if (ctx->tln_explore_hint == -1)
                mcts_set_exploration(cur_c - 0.05f);
        }

        /* TLN 观察模式: 所有 hint=0 → 悬置态，暂停变异，加速学习 */
        if (tln_is_observing(&ctx->tln)) {
            /* P0-1: 冷却期内不重入观察 */
            if (ctx->observe_cooldown > 0) {
                ctx->observe_cooldown--;
            } else {
                tln_observe_tick(&ctx->tln);

                /* P0-1: 心跳加速使用标志位，不是单帧检测 */
                if (!ctx->heartbeat_fastened) {
                    soul_set_heartbeat_ms(ctx->soul, 100);
                    ctx->heartbeat_fastened = 1;
                    if (!ctx->quiet)
                        printf("[%4d] OBSERVE: heartbeat accelerated to 100ms\n", ctx->round);
                }

                /* 观察模式: 每 tick 多做一次 soul_read (加速感知) */
                soul_read(ctx->soul);
                /* 观察模式: 每 5 tick 记录一次环境快照 */
                if (ctx->round % 5 == 0) {
                    uint8_t obs_snap[SOUL_SIZE];
                    memcpy(obs_snap, ctx->soul->buf, SOUL_SIZE);
                    snap_auto(inp->tick, (int64_t)ctx->drive, inp->hw_stress,
                              soul_gen_count(ctx->soul), obs_snap);
                    if (!ctx->quiet && ctx->round % 50 == 0)
                        printf("[%4d] tick=%-6u OBSERVE: snapshot (observe_ticks=%u)\n",
                               ctx->round, inp->tick, ctx->tln.observe_ticks);
                }

                /* P0-1: 120 tick 超时 → 强制退出 + 冷却期 */
                if (tln_observe_timed_out(&ctx->tln)) {
                    uint32_t ec = soul_experience_count(ctx->soul) + 1;
                    soul_write_buf(ctx->soul, S_EXPERIENCE_COUNT, &ec, 4);
                    ctx->tln_action_hint = -1;
                    int8_t force_conservative = -1;
                    soul_write_buf(ctx->soul, S_TLN_ACTION, &force_conservative, 1);
                    tln_observe_reset(&ctx->tln);
                    soul_set_heartbeat_ms(ctx->soul, 500);
                    ctx->heartbeat_fastened = 0;
                    ctx->observe_cooldown = 30;
                    if (!ctx->quiet)
                        printf("[%4d] OBSERVE TIMEOUT: force conservative, cooldown=30\n", ctx->round);
                }
            }
        } else if (ctx->tln.observe_ticks > 0 || ctx->heartbeat_fastened) {
            /* 退出观察模式: 恢复心跳 */
            if (ctx->heartbeat_fastened) {
                soul_set_heartbeat_ms(ctx->soul, 500);
                ctx->heartbeat_fastened = 0;
            }
            tln_observe_reset(&ctx->tln);
        }
    }
}
