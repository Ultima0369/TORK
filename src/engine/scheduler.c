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
#include "swarm.h"
#include "visual.h"
#include "fractal.h"
#include "task.h"
#include "auditor.h"
#include "../persist/code_archive.h"
#include "../code/strict_verifier.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* Aurora: 最近一次本能评估结果 */
static tork_instinct_t g_last_inst = {0, 0, 0};

const tork_instinct_t *sched_last_instinct(void) {
    return &g_last_inst;
}

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
    inp->peer_count = 0;  /* 确保首 tick 有初始值 */

    /* ── BEACON BROADCAST (every 1000 tick) ──
     *
     * 分形基元四步推导：
     *
     * 1) 比较 (Compare)
     *    当前信标路径：16×memcpy(4B) → pi_compute_digest(call) → inline rdtsc
     *    → beacon_broadcast(sendto) → beacon_prune(O(n)+time()+mutex)
     *    理想路径：1×memcpy(64B) → 内联XOR循环 → 读Soul缓存TSC
     *    → beacon_broadcast(sendto) → 分频prune
     *
     * 2) 差异 (Difference)
     *    a) 16×memcpy(4B) = 16×call帧开销 vs 1×memcpy(64B) = 编译器可优化为rep movsb
     *    b) pi_compute_digest跨编译单元调用不可内联 vs 内联后memset+循环编译器可融合
     *    c) inline rdtsc串行化管道(~25周期) vs soul_cur_tsc从本地缓存读取(1 mov)
     *    d) beacon_prune每1000tick(time()+mutex+O(n)) vs 每10000tick(开销降90%)
     *
     * 3) 模糊容忍 (Blur)
     *    a) 1×memcpy(64B)与16×memcpy(4B)的字节序结果恒等
     *    b) 内联XOR循环与pi_compute_digest函数的数学结果恒等
     *    c) Soul中CUR_TSC由ASM每tick写入rdtsc，差值<1 tick周期，对信标物理熵无影响
     *    d) prune延迟10tick≈5秒，相对BEACON_EXPIRE_S=300秒的误差率1.67%，功能影响为零
     *
     * 4) 归纳类似 (Similar)
     *    小尺寸离散复制→批量复制；跨编译单元函数→内联；物理时钟读取→复用缓存值；
     *    高频低效清理→分频批处理——与TLN推理/模式学习的分频策略同构
     */
    if (i % BEACON_INTERVAL == 0) {
        uint8_t pi_digest[16];

        {
            uint32_t colony_seed[16];
            memcpy(colony_seed, ctx->soul->buf, sizeof(colony_seed));

            memset(pi_digest, 0, sizeof(pi_digest));
            for (int si = 0; si < 16; si++) {
                uint32_t v = colony_seed[si];
                int idx = si & 3;
                pi_digest[idx]         ^= (uint8_t)(v);
                pi_digest[idx + 4]     ^= (uint8_t)(v >> 8);
                pi_digest[idx + 8]     ^= (uint8_t)(v >> 16);
                pi_digest[idx + 12]    ^= (uint8_t)(v >> 24);
            }
        }

        uint32_t tsc_lo = (uint32_t)soul_cur_tsc(ctx->soul);

        beacon_broadcast(ctx->soul, pi_digest, tsc_lo);

        if (i % (BEACON_INTERVAL * 10) == 0)
            beacon_prune(NULL);
    }

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

    /* Inductive: extract (every 800) */
    if (i % 800 == 0) tick_inductive(ctx);

    /* Inductive: test rule (every 1000) */
    if (i % 1000 == 0) tick_inductive_test(ctx);

    /* Persistence (1000/5000/10000) */
    tick_persist(ctx);

    /* Swarm: 更新同类感知 */
    inp->peer_count = swarm_beacon_count();
    int dist_cnt = swarm_dist_count();
    if (dist_cnt > inp->peer_count) inp->peer_count = dist_cnt;
    /* Re-evaluate instinct (every 10 ticks) */
    if (ctx->round % 10 == 0) {
        ctx->inst = instinct_evaluate(inp);
        g_last_inst = ctx->inst;
        ctx->idle_discoveries = 0;
    }
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

    /* Pattern query */
    {
        float pat_conf = 0.0f;
        int8_t prev_drive = soul_drive(soul);
        int pat_action = pat_query_best_action(inp->hw_stress,
            prev_drive, inp->fission_count, &pat_conf);
        if (pat_action >= 0 && pat_conf > 0.0f) {
            inp->pattern_best_action = pat_action;
            inp->pattern_confidence  = pat_conf;
            if (!ctx->quiet) {
                static int last_pat_action = -1;
                static int last_conf_bucket = -1;
                int bucket = (int)(pat_conf * 10.0f);
                if (pat_action != last_pat_action || bucket != last_conf_bucket) {
                    printf("  PATTERN: action=%d conf=%.3f (drive=%d)\n", pat_action, pat_conf, (int)prev_drive);
                    last_pat_action = pat_action;
                    last_conf_bucket = bucket;
                }
            }
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

    /* Visual: 生成一帧状态画像 */
    visual_tick(inp->tick, (uint8_t)ctx->drive, ctx->inst.fear, ctx->inst.desire,
               ctx->inst.curiosity, inp->peer_count);
    /* Print state */
    tick_print(ctx);

    inp->branch_active_count = br_active_count();
}
