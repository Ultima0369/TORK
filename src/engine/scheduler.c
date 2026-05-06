#include "scheduler.h"
#include "sched_services.h"
#include "sched_tln.h"
#include "sched_code_ops.h"
#include "sched_fission_branch.h"
#include "sched_inductive.h"
#include "sched_persist.h"
#include "sched_monitor.h"
#include "sched_idle.h"
#include "code_reader.h"
#include "monitor.h"
#include "fission.h"
#include "blackboard.h"
#include "inductor.h"
#include "persistor.h"
#include "idler.h"
#include "dispatch.h"
#include "codegen.h"
#include "torkd.h"
#include "../learning/experience.h"
#include "../learning/mcts.h"
#include "../learning/branch.h"
#include "../learning/pattern.h"
#include "../learning/self_tune.h"
#include "../learning/mentor.h"
#include "../learning/observer.h"
#include "../learning/snapshot.h"
#include "../learning/energy.h"
#include "../learning/watcher.h"
#include "../learning/self_build.h"
#include "../learning/mutation_guide.h"
#include "../learning/self_cal.h"
#include "../learning/distributed.h"
#include "../learning/pi_seed.h"
#include "../learning/pi_index.h"
#include "../grid/grid_soul_connector.h"
#include "beacon.h"
#include "fractal.h"
#include "task.h"
#include "auditor.h"
#include "../persist/code_archive.h"
#include "../code/strict_verifier.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

void scheduler_init(sched_ctx_t *ctx, soul_t *soul, int quiet) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->soul = soul;
    ctx->quiet = quiet;
    ps_register_soul_buf(soul->buf, sizeof(soul->buf));
    ctx->prev_bb_opt_at_idle = 1;  /* non-zero to avoid false early trigger */

    /* TLN: 初始化三进制逻辑网络 */
    tln_init(&ctx->tln);
    if (tln_load(&ctx->tln, NULL) == 0) {
        ctx->tln_enabled = 1;
        printf("  TLN: loaded from persist/tln.bin\n");
    } else {
        /* 新网络: 用稀疏随机权重初始化 (5% 密度) */
        tln_mutate(&ctx->tln, 0.05f);
        ctx->tln_enabled = 1;
        printf("  TLN: initialized with sparse random weights\n");
    }

    /* 师徒阶段管理器 */
    mentor_init();
    printf("  MENTOR: stage=%s\n", mentor_stage_name(mentor_get_stage()));

    /* CODE-4: 代码生存档案 + 严格语法验证器 */
    ca_init();
    sv_init();
    printf("  CODE_ARCHIVE: initialized\n");
    printf("  STRICT_VERIFIER: initialized\n");
}

/* ── Pattern learning (every 20 ticks) ── */
static void tick_pattern_learn(sched_ctx_t *ctx) {
    (void)ctx;
    static int learn_counter = 0;
    learn_counter++;
    if (learn_counter % 20 == 0 && exp_count() > 5) {
        pat_learn_from_experience();
        tune_adjust_from_patterns();
        instinct_apply_tune();
        if (learn_counter % 40 == 0)
            pat_save();
    }
}

/* ── Print state (every 10) ── */
static void tick_print(sched_ctx_t *ctx) {
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    if (i % 10 == 0) {
        uint32_t bb_opt = bb_global_optimizations();
        printf("[%4d] tick=%-6u pid=%-5u ppid=%-4u drive=%+4d fear=%.1f desire=%.1f curiosity=%.1f fission=%d wins=%d bb_opts=%u\n",
               i, inp->tick, soul_self_pid(ctx->soul), soul_ppid(ctx->soul),
               drive, ctx->inst.fear, ctx->inst.desire, ctx->inst.curiosity,
               inp->fission_count, inp->wins, bb_opt);
    }
    if (i % 100 == 0) {
        uint32_t bb_opt = bb_global_optimizations();
        uint32_t bb_fis = bb_global_fissions();
        uint32_t bb_err = bb_global_errors();
        printf("[%4d] tick=%-6u BB: opts=%u fissions=%u errors=%u\n",
               i, inp->tick, bb_opt, bb_fis, bb_err);
        if (ctx->tln_enabled)
            tln_print_state(&ctx->tln);
    }
}

/* ══════════════════════════════════════════════════════════════
 * Main scheduler entry — called once per loop iteration
 * ══════════════════════════════════════════════════════════════ */
void scheduler_tick(sched_ctx_t *ctx) {
    int i = ctx->round;
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;

    /* Staleness detection */
    {
        uint8_t cur_hw = inp->hw_stress;
        int8_t cur_drive = (int8_t)soul_drive(soul);
        int hw_changed = (cur_hw != ctx->prev_hw_stress);
        int drive_stuck = (cur_drive == ctx->prev_drive_val);
        if (hw_changed && drive_stuck)
            ctx->env_stagnation_count++;
        else if (!drive_stuck)
            ctx->env_stagnation_count = 0;
        if (ctx->env_stagnation_count >= 3) {
            inp->env_changed = 1;
            ctx->env_stagnation_count = 0;
        }
        ctx->prev_hw_stress = cur_hw;
        ctx->prev_drive_val = cur_drive;
    }

    bb_set_tick(inp->tick);

    /* Track blackboard activity */
    uint32_t prev_bb_opt = inp->bb_global_opts;
    uint32_t prev_bb_fis = bb_global_fissions();
    (void)prev_bb_fis;

    int mod_cycle = 10;
    int opt_cycle = 30;
    int nop_cycle = 50;

    /* Pattern learning (every 20 internally) */
    tick_pattern_learn(ctx);

    /* CODE-4: 代码生存档案 — 每 tick 更新存活时间 */
    ca_tick_alive(inp->tick);
    {
        uint32_t alive = ca_alive_ticks();
        if (alive > 0 && alive % 1000 == 0) {
            /* 存活>1000心跳: 触发周期性模式学习 + 自动录入成功模式 */
            pat_learn_from_experience();
            ca_record_t cur;
            int ai = ca_query_hash(0);
            uint32_t hash = 0;
            if (ai >= 0 && ca_read(ai, &cur) == 0)
                hash = cur.code_hash;
            pat_record_success(hash, alive, 0, "auto-survival");
            printf("[%4d] tick=%-6u CODE_ARCHIVE: survival=%u → pattern learned+recorded\n",
                   i, inp->tick, alive);
        }
    }

    /* Per-tick services */
    tick_services(ctx);

    /* Code reading (every 200) */
    if (i % 200 == 0) tick_code_read(ctx);

    /* Code modification — TLN 可以否决，观察模式暂停变异，黄金恢复观察期禁止变异 */
    if (i % mod_cycle == 0) {
        if (ctx->golden_observe_remaining > 0) {
            if (!ctx->quiet && i % 100 == 0)
                printf("[%4d] OBSERVE: mutation suspended (golden restore observation, %d ticks remaining)\n", i, ctx->golden_observe_remaining);
        } else if (ctx->tln_enabled && tln_is_observing(&ctx->tln)) {
            /* 观察模式: 不动手，先看 */
            if (!ctx->quiet && i % 100 == 0)
                printf("[%4d] OBSERVE: mutation suspended (tln all-zero)\n", i);
        } else if (!ctx->tln_enabled || ctx->tln_modify_hint != -1)
            tick_code_modify(ctx);
    }

    /* Code optimization — 观察模式也暂停优化 */
    if (i % opt_cycle == 0) {
        if (ctx->golden_observe_remaining <= 0 && !(ctx->tln_enabled && tln_is_observing(&ctx->tln)))
            tick_code_optimize(ctx);
    }

    /* NOP deletion — 观察模式暂停 */
    if (i % nop_cycle == 0) {
        if (ctx->golden_observe_remaining <= 0 && !(ctx->tln_enabled && tln_is_observing(&ctx->tln)))
            tick_nop_delete(ctx);
    }

    /* Inductive: apply rule (every 1200) */
    if (i % 1200 == 0) tick_inductive_apply(ctx);

    /* Fission (every 1000) */
    if (i % 1000 == 0) tick_fission(ctx);

    /* Beacon: 同类识别广播 (every 1000) */
    if (i % BEACON_INTERVAL == 0) {
        uint32_t colony_seed[16];
        for (int si = 0; si < 16; si++)
            memcpy(&colony_seed[si], ctx->soul->buf + si * 4, 4);
        uint8_t pi_digest[16];
        pi_compute_digest(colony_seed, pi_digest);
        uint64_t tsc;
        __asm__ __volatile__("rdtsc" : "=A"(tsc));
        beacon_broadcast(ctx->soul, pi_digest, (uint32_t)tsc);
        beacon_prune(NULL);
    }

    /* Inductive: extract (every 800) */
    if (i % 800 == 0) tick_inductive(ctx);

    /* Inductive: test rule (every 1000) */
    if (i % 1000 == 0) tick_inductive_test(ctx);

    /* Persistence (1000/5000/10000) */
    tick_persist(ctx);

    /* Re-evaluate instinct */
    ctx->inst = instinct_evaluate(inp);
    ctx->idle_discoveries = 0;
    ctx->drive = (int)((ctx->inst.desire - ctx->inst.fear + ctx->inst.curiosity) * 100.0f);
    if (ctx->drive > DRIVE_MAX) ctx->drive = DRIVE_MAX;
    if (ctx->drive < DRIVE_MIN) ctx->drive = DRIVE_MIN;

    /* TLN 调制: 三值逻辑推理修正 drive 方向 */
    if (ctx->tln_enabled) {
        if (ctx->tln_action_hint == 1)  ctx->drive += 8;   /* TLN: 激进 */
        if (ctx->tln_action_hint == -1) ctx->drive -= 8;   /* TLN: 保守 */
        /* 0 = 悬置，不动 */
        if (ctx->drive > DRIVE_MAX) ctx->drive = DRIVE_MAX;
        if (ctx->drive < DRIVE_MIN) ctx->drive = DRIVE_MIN;
    }
    soul_set_drive(soul, (int8_t)ctx->drive);

    /* Pattern query */
    {
        float pat_conf = 0.0f;
        int8_t prev_drive = soul_drive(soul);
        int pat_action = pat_query_best_action(inp->hw_stress,
            prev_drive, inp->fission_count, &pat_conf);
        if (pat_action >= 0 && pat_conf > 0.0f) {
            inp->pattern_best_action = pat_action;
            inp->pattern_confidence  = pat_conf;
            if (!ctx->quiet) printf("  PATTERN: action=%d conf=%.3f (drive=%d)\n", pat_action, pat_conf, (int)prev_drive);
        }
    }

    /* Branch fork + advance */
    tick_branch(ctx);

    /* π-seed + rhythm */
    tick_pi_rhythm(ctx);

    /* Observer + energy */
    tick_observer_energy(ctx);

    /* Monitoring */
    tick_monitoring(ctx);

    /* Snapshot + health */
    tick_snapshot(ctx);

    /* Update branch info in inp */
    {
        uint32_t lft = br_last_fork_tick();
        inp->branch_fork_ticks_ago = (lft > 0) ? (int)(inp->tick - lft) : -1;
    }
    {
        reap_report_t rr = br_last_reap();
        inp->branch_reap_just_happened = (rr.death_reason != 0 && rr.branch_id > 0);
    }

    /* Update last_bb_tick */
    {
        uint32_t cur_bb_opt = bb_global_optimizations();
        uint32_t cur_bb_fis = bb_global_fissions();
        if (cur_bb_opt != prev_bb_opt || cur_bb_fis != prev_bb_fis)
            ctx->last_bb_tick = inp->tick;
    }

    /* Feedback */
    tick_feedback(ctx);

    /* Idle (every 100) */
    if (i % 100 == 0 && i > 0) tick_idle(ctx);

    /* TLN 进化: 每 2000 tick 做一次微小变异 (0.5% 权重)
     * P0-1: observe 期间禁止变异 */
    if (ctx->tln_enabled && i > 0 && i % 2000 == 0 && !tln_is_observing(&ctx->tln)) {
        int m = tln_mutate(&ctx->tln, 0.005f);
        if (m > 0 && !ctx->quiet)
            printf("[%4d] tick=%-6u TLN: evolved %d weights\n", i, inp->tick, m);
    }

    /* Print state */
    tick_print(ctx);

    inp->branch_active_count = br_active_count();
}
