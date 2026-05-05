#include "scheduler.h"
#include "code_reader.h"
#include "code_modifier.h"
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
#include "task.h"
#include "auditor.h"
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
}

/* ── Per-tick services ── */
static void tick_services(sched_ctx_t *ctx) {
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

    /* TLN: 每 tick 做一步三值逻辑推理 */
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
    }
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

/* ── Code reading (every 200) ── */
static void tick_code_read(sched_ctx_t *ctx) {
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    char asm_buf[8192];
    int alen = asm_read_file("benchmark/memcpy/ref.s", asm_buf, sizeof(asm_buf));
    if (alen <= 0) return;

    int insns = asm_count_insns_in_func(asm_buf, alen, "memcpy_tork");
    char opcodes[32][8];
    asm_extract_opcodes(asm_buf, alen, "memcpy_tork", opcodes, 32);
    int cm = 0, ca = 0, cc = 0, co = 0;
    asm_classify_insns(asm_buf, alen, "memcpy_tork", &cm, &ca, &cc, &co);

    printf("[%4d] tick=%-6u reading memcpy_tork: %d insns\n", i, inp->tick, insns);
    printf("       opcodes:");
    int show = (insns > 10) ? 10 : insns;
    for (int k = 0; k < show; k++) printf(" %s", opcodes[k]);
    printf("\n       class: mov=%d arith=%d control=%d other=%d\n", cm, ca, cc, co);

    uint8_t stats[10];
    memset(stats, 0, sizeof(stats));
    *(uint16_t*)(stats + 0) = (uint16_t)insns;
    *(uint16_t*)(stats + 2) = (uint16_t)cm;
    *(uint16_t*)(stats + 4) = (uint16_t)ca;
    *(uint16_t*)(stats + 6) = (uint16_t)cc;
    *(uint16_t*)(stats + 8) = (uint16_t)co;
    soul_write_buf(soul, S_CODE_INSNS, stats, 10);
    inp->code_insns = (uint16_t)insns;
    inp->code_ctrl  = (uint16_t)cc;
}

/* ── Code modification: je→jz (mod_cycle) ── */
static void tick_code_modify(sched_ctx_t *ctx) {
    if (ctx->mod_attempted) return;
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    char asm_buf[8192];
    int alen = asm_read_file("benchmark/memcpy/ref.s", asm_buf, sizeof(asm_buf));
    if (alen <= 0) return;

    char backup[8192];
    int backup_len = alen;
    memcpy(backup, asm_buf, alen);

    int rep_len = alen;
    int rep = asm_replace_operand(asm_buf, alen, (int)sizeof(asm_buf), "memcpy_tork", "\tje\t", "\tjz\t", 1, &rep_len);
    if (rep == 1) {
        if (asm_verify_modification(asm_buf, rep_len, "benchmark/memcpy")) {
            FILE *f = fopen("benchmark/memcpy/ref.s", "w");
            if (f) { fwrite(asm_buf, 1, rep_len, f); fclose(f); }
            printf("[%4d] tick=%-6u MODIFY SUCCESS: replaced je with jz\n", i, inp->tick);
            uint8_t ms = 1;
            exp_update_last(80, 0, 1, inp->hw_stress, drive);
            soul_write_buf(soul, S_CODE_MOD_SUCCESS, &ms, 1);
            inp->code_mod_success = 1;
            bb_write(BB_TYPE_OPT_SUCCESS, 1, (uint32_t)i);
            bb_inc_optimizations();
            ctx->rounds_since_mod = 0;
            ctx->last_bb_tick = inp->tick;
        } else {
            asm_rollback(asm_buf, sizeof(asm_buf), backup, backup_len);
            printf("[%4d] tick=%-6u MODIFY FAILED: je→jz rejected\n", i, inp->tick);
            uint8_t ms = 2;
            exp_update_last(-30, 0, 0, inp->hw_stress, drive);
            soul_write_buf(soul, S_CODE_MOD_SUCCESS, &ms, 1);
            inp->code_mod_success = 2;
            bb_write(BB_TYPE_OPT_FAIL, 1, (uint32_t)i);
        }
    } else {
        printf("[%4d] tick=%-6u MODIFY SKIP: je not found\n", i, inp->tick);
    }
    ctx->mod_attempted = 1;
}

/* ── Dead code optimization (opt_cycle) ── */
static void tick_code_optimize(sched_ctx_t *ctx) {
    if (ctx->opt_attempted) return;
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    char asm_buf[8192];
    int alen = asm_read_file("benchmark/memcpy/ref.s", asm_buf, sizeof(asm_buf));
    if (alen <= 0) return;

    char backup[8192];
    int backup_len = alen;
    memcpy(backup, asm_buf, alen);

    printf("[%4d] tick=%-6u OPT: scanning for dead code...\n", i, inp->tick);
    int new_len = alen;
    int deleted = asm_delete_dead_insns(asm_buf, alen, "memcpy_tork", &new_len);
    if (deleted > 0) {
        if (asm_verify_modification(asm_buf, new_len, "benchmark/memcpy")) {
            FILE *f = fopen("benchmark/memcpy/ref.s", "w");
            if (f) { fwrite(asm_buf, 1, new_len, f); fclose(f); }
            printf("[%4d] tick=%-6u OPT: deleted %d dead insn(s) after ret\n", i, inp->tick, deleted);
            exp_update_last(70, 0, 1, inp->hw_stress, drive);
            uint8_t sv = (uint8_t)deleted;
            soul_write_buf(soul, S_CODE_OPT_SAVED, &sv, 1);
            inp->code_opt_saved = sv;
            bb_write(BB_TYPE_OPT_SUCCESS, 2, (uint32_t)deleted);
            bb_inc_optimizations();
        } else {
            asm_rollback(asm_buf, sizeof(asm_buf), backup, backup_len);
            printf("[%4d] tick=%-6u OPT: deleted %d but verification FAILED, rollback\n", i, inp->tick, deleted);
            exp_update_last(-20, 1, 0, inp->hw_stress, drive);
        }
    } else {
        printf("[%4d] tick=%-6u OPT: no dead code found\n", i, inp->tick);
    }
    ctx->opt_attempted = 1;
}

/* ── NOP deletion (nop_cycle) ── */
static void tick_nop_delete(sched_ctx_t *ctx) {
    if (ctx->nop_attempted) return;
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    char asm_buf[8192];
    int alen = asm_read_file("benchmark/memcpy/ref.s", asm_buf, sizeof(asm_buf));
    if (alen <= 0) return;

    char backup[8192];
    int backup_len = alen;
    memcpy(backup, asm_buf, alen);

    int new_len = alen;
    int nops = asm_delete_nop_insns(asm_buf, alen, "memcpy_tork", &new_len);
    if (nops > 0) {
        if (asm_verify_modification(asm_buf, new_len, "benchmark/memcpy")) {
            FILE *f = fopen("benchmark/memcpy/ref.s", "w");
            if (f) { fwrite(asm_buf, 1, new_len, f); fclose(f); }
            uint8_t total = inp->code_opt_saved + (uint8_t)nops;
            soul_write_buf(soul, S_CODE_OPT_SAVED, &total, 1);
            uint8_t nc = (uint8_t)nops;
            soul_write_buf(soul, S_CODE_NOP_COUNT, &nc, 1);
            inp->code_opt_saved = total;
            inp->code_nop_count = nc;
            printf("[%4d] tick=%-6u NOP: verification PASSED, saved %d lines total\n", i, inp->tick, total);
            bb_write(BB_TYPE_OPT_SUCCESS, 3, (uint32_t)nops);
            bb_inc_optimizations();
        } else {
            asm_rollback(asm_buf, sizeof(asm_buf), backup, backup_len);
            printf("[%4d] tick=%-6u NOP: deleted %d but verification FAILED, rollback\n", i, inp->tick, nops);
            exp_update_last(-20, 1, 0, inp->hw_stress, drive);
        }
    } else {
        printf("[%4d] tick=%-6u NOP: no nop insns found in memcpy_tork\n", i, inp->tick);
        uint8_t nc = 0;
        soul_write_buf(soul, S_CODE_NOP_COUNT, &nc, 1);
        inp->code_nop_count = 0;
    }
    ctx->nop_attempted = 1;
}

/* ── Fission (every 1000) ── */
static void tick_fission(sched_ctx_t *ctx) {
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    if (i <= 0) return;
    if (!fission_decide(soul) || ctx->inst.curiosity <= 0.6f) {
        printf("[%4d] tick=%-6u FISSION: conditions not met (tick=%u drive=%+d curiosity=%.1f)\n",
               i, inp->tick, inp->tick, drive, ctx->inst.curiosity);
        return;
    }

    printf("[%4d] tick=%-6u FISSION: instinct triggers fission\n", i, inp->tick);
    dispatch_input_t fdin;
    memset(&fdin, 0, sizeof(fdin));
    fdin.action = DISP_SELF_FISSION;
    fdin.input = "fission_trigger";
    fdin.tick = inp->tick;
    fdin.hw_stress = inp->hw_stress;
    fdin.drive = (int8_t)drive;
    fdin.gen_count = inp->fission_count;
    dispatch_output_t fdout = tork_dispatch(&fdin);
    (void)fdout;

    pid_t child = fission_spawn();
    if (child > 0) {
        fdout.rc = 0;
        uint16_t cp = (uint16_t)child;
        soul_write_buf(soul, S_CHILD_PID, &cp, 2);
        uint8_t fc = inp->fission_count + 1;
        soul_write_buf(soul, S_FISSION_COUNT, &fc, 1);
        uint16_t ft = (uint16_t)inp->tick;
        soul_write_buf(soul, S_FISSION_TICK, &ft, 2);
        inp->fission_count = fc;
        printf("[%4d] tick=%-6u FISSION: child spawned PID=%d\n", i, inp->tick, child);
        bb_write(BB_TYPE_FISSION, 1, (uint32_t)child);
        bb_inc_fissions();

        if (fission_migrate(child) == 0) {
            uint16_t w = inp->wins + 1;
            soul_write_buf(soul, S_WINS, &w, 2);
            inp->wins = w;
            uint16_t zero16 = 0;
            soul_write_buf(soul, S_CHILD_PID, &zero16, 2);
            printf("[%4d] tick=%-6u FISSION: parent won, wins=%d\n", i, inp->tick, w);
        }
    } else {
        printf("[%4d] tick=%-6u FISSION: spawn failed\n", i, inp->tick);
    }
}

/* ── Inductive learning (every 800) ── */
static void tick_inductive(sched_ctx_t *ctx) {
    int i = ctx->round;
    if (i <= 0) return;

    struct tork_rule experiences[8];
    int n = ind_extract_experiences(experiences, 8);
    if (n >= 2) {
        struct tork_rule generalized;
        if (ind_generalize(experiences, n, &generalized) == 0) {
            int slot = ind_save_rule(&generalized);
            printf("[%4d] IND: generalized rule from %d experiences → slot %d "
                   "premise='%.32s' confidence=%d%%\n",
                   i, n, slot, generalized.premise, generalized.confidence);
        }
    } else if (n == 1) {
        int slot = ind_save_rule(&experiences[0]);
        if (slot >= 0)
            printf("[%4d] IND: extracted 1 experience → slot %d\n", i, slot);
    }
}

/* ── Test pending rule (every 1000) ── */
static void tick_inductive_test(sched_ctx_t *ctx) {
    int i = ctx->round;
    if (i <= 0) return;

    int slot = ind_find_pending();
    if (slot < 0) return;

    struct tork_rule all[32];
    ind_load_rules(all, 32);
    struct tork_rule rule = all[slot];
    int result = ind_test_rule(&rule, "benchmark/memcpy/ref.s");
    ind_update_rule(slot, &rule);
    printf("[%4d] IND: tested rule slot %d → %s confidence=%d%% active=%d\n",
           i, slot, result == 0 ? "PASS" : "FAIL", rule.confidence, rule.active);
}

/* ── Apply active rule (every 1200) ── */
static void tick_inductive_apply(sched_ctx_t *ctx) {
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    if (i <= 0) return;

    int slot = ind_find_active();
    if (slot < 0) return;

    struct tork_rule all[32];
    ind_load_rules(all, 32);
    struct tork_rule rule = all[slot];
    printf("[%4d] tick=%-6u RULE: applying active rule slot %d "
           "'%.32s' → '%.32s'\n", i, inp->tick, slot, rule.premise, rule.conclusion);
    int result = ind_test_rule(&rule, "benchmark/memcpy/ref.s");
    ind_update_rule(slot, &rule);
    if (result == 0) {
        inp->rule_applied = 1;
        printf("[%4d] tick=%-6u RULE: application PASSED\n", i, inp->tick);
    } else {
        printf("[%4d] tick=%-6u RULE: application FAILED or not applicable\n", i, inp->tick);
    }
}

/* ── Persistence (every 1000/5000/10000) ── */
static void tick_persist(sched_ctx_t *ctx) {
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    if (i <= 0) return;

    if (i % 1000 == 0) {
        if (ps_save_all(soul->buf, SOUL_SIZE) == 0) {
            inp->save_success = 1;
            printf("[%4d] tick=%-6u PERSIST: state saved to disk\n", i, inp->tick);
        }
    }
    if (i % 5000 == 0) {
        if (ps_save_all(soul->buf, SOUL_SIZE) == 0) {
            inp->save_success = 1;
            printf("[%4d] tick=%-6u PERSIST: full save (params+rules)\n", i, inp->tick);
        }
        ps_decay_memory();
    }
    if (i % 10000 == 0) {
        ps_decay_memory();
    }

    /* TLN 持久化 (every 5000) */
    if (ctx->tln_enabled && i % 5000 == 0) {
        if (tln_save(&ctx->tln, NULL) == 0)
            printf("[%4d] tick=%-6u PERSIST: TLN saved\n", i, inp->tick);
    }
}

/* ── Watcher, self-build, mutation guide ── */
static void tick_monitoring(sched_ctx_t *ctx) {
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    if (i > 0 && i % 10 == 0)
        watcher_scan_proc();

    if (i > 0 && i % 100 == 0) {
        int new_pats = watcher_learn_patterns();
        if (new_pats > 0)
            printf("[%4d] tick=%-6u WATCH: learned %d new patterns\n", i, inp->tick, new_pats);
    }

    if (i > 0 && i % 50 == 0) {
        int changed = sb_check_sources();
        if (changed > 0)
            printf("[%4d] tick=%-6u SELF: %d source files changed\n", i, inp->tick, changed);
    }

    if (i > 0 && i % 200 == 0 && drive > 0) {
        char rec[128];
        mg_strategy_type_t s = mg_recommend(rec, sizeof(rec));
        (void)s;
        if (i % 1000 == 0)
            printf("[%4d] tick=%-6u MGUIDE: recommend %s\n", i, inp->tick, rec);
    }
}

/* ── Observer + Energy ── */
static void tick_observer_energy(sched_ctx_t *ctx) {
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    if (inp->tick % OBS_SAMPLE_INTERVAL == 0) {
        obs_sample(inp->tick, inp->hw_stress, (int8_t)drive, 47, 30,
                   (uint16_t)br_active_count());
        int anomaly = obs_check_anomaly(inp->hw_stress, (int8_t)drive, 47, 30);
        if (anomaly > 1)
            printf("[%4d] tick=%-6u OBS: anomaly detected (flags=%d)\n", i, inp->tick, anomaly);
    }

    {
        float hb_mult = self_cal_tick(inp->tick, 30.0f, 4096.0f, inp->hw_stress, exp_count(), 0);
        eng_update(30.0f, 4096.0f, 0.1f, (drive == 0));
        if (hb_mult != 1.0f && (i % 10 == 0))
            printf("[%4d] tick=%-6u ENG: heartbeat mult=%.1f\n", i, inp->tick, hb_mult);
        if (eng_should_limit_branches() && br_active_count() > 2) {
            /* don't fork when loaded */
        }
    }
}

/* ── Snapshot + health check ── */
static void tick_snapshot(sched_ctx_t *ctx) {
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    {
        uint8_t soul_raw[SOUL_SIZE];
        memset(soul_raw, 0, SOUL_SIZE);
        memcpy(soul_raw, soul->buf, SOUL_SIZE);
        snap_auto(inp->tick, (int64_t)drive, inp->hw_stress,
                  soul_gen_count(soul), soul_raw);
    }

    {
        health_check_t hc = snap_health_check(inp->tick, (int64_t)drive, inp->hw_stress, 1);
        if (!hc.degraded && inp->tick > 0 && inp->tick % (SNAP_AUTO_INTERVAL * 8) == 0) {
            uint8_t soul_raw[SOUL_SIZE];
            memset(soul_raw, 0, SOUL_SIZE);
            memcpy(soul_raw, soul->buf, SOUL_SIZE);
            snap_commit(inp->tick, (int64_t)drive, inp->hw_stress,
                       soul_gen_count(soul), soul_raw);
        }
        if (hc.degraded && soul_self_pid(soul) > 0) {
            printf("[%4d] tick=%-6u SNAP: DEGRADED (drive_drop=%.0f), ROLLING BACK\n",
                   i, inp->tick, hc.drive_drop);
            uint8_t restored_soul[SOUL_SIZE];
            uint32_t restore_tick = 0;
            int r = snap_rollback(restored_soul, &restore_tick);
            if (r > 0 && restore_tick > 0 && soul->wr_fd >= 0) {
                soul_write_buf(soul, 0, restored_soul, SOUL_SIZE);
                printf("[%4d] tick=%-6u SNAP: wrote %d bytes to core via ptrace\n",
                       i, inp->tick, r);
                soul_read(soul);
            }
        }
    }
}

/* ── π-seed + rhythm ── */
static void tick_pi_rhythm(sched_ctx_t *ctx) {
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int *drive = &ctx->drive;

    if (!ctx->rhythm_inited) {
        pi_rhythm_init(&ctx->rhythm);
        ctx->rhythm_inited = 1;
    }

    uint8_t pi_val = pi_seed_from_tsc();
    float pi_mid = pi_seed_float();
    static uint8_t prev_pi_val = 0;

    (void)pi_drift(pi_val, prev_pi_val);

    if (*drive == 0 || *drive == DRIVE_MAX || *drive == DRIVE_MIN) {
        int8_t nudge = (int8_t)(pi_mid * 5.0f) - 2;
        int new_drive = *drive + nudge;
        if (new_drive > DRIVE_MAX) new_drive = DRIVE_MAX;
        if (new_drive < DRIVE_MIN) new_drive = DRIVE_MIN;
        soul_write_byte(soul, S_DRIVE, (uint8_t)(int8_t)new_drive);
        *drive = (int8_t)new_drive;
    }

    {
        int8_t drive_delta = (int8_t)*drive - ctx->prev_drive_val;
        pi_rhythm_observe(&ctx->rhythm, pi_val, drive_delta);

        float dissonance = pi_rhythm_dissonance(&ctx->rhythm);
        if (dissonance > 0.7f && i % 50 == 0) {
            int8_t kick = (int8_t)(pi_mid * 20.0f) - 10;
            int new_drive = *drive + kick;
            if (new_drive > DRIVE_MAX) new_drive = DRIVE_MAX;
            if (new_drive < DRIVE_MIN) new_drive = DRIVE_MIN;
            soul_write_byte(soul, S_DRIVE, (uint8_t)(int8_t)new_drive);
            *drive = (int8_t)new_drive;
        }

        if (ctx->rhythm.count >= 8 && i % 20 == 0) {
            r_zone_t zone = pi_r_zone(&ctx->rhythm);
            if (zone == R_DEAD && !ctx->quiet) {
                printf("  R-ZONE: DEAD (R<3.0) — 打破僵死\n");
                int8_t kick = (int8_t)(pi_mid * 40.0f) - 20;
                int new_drive = *drive + kick;
                if (new_drive > DRIVE_MAX) new_drive = DRIVE_MAX;
                if (new_drive < DRIVE_MIN) new_drive = DRIVE_MIN;
                soul_write_byte(soul, S_DRIVE, (uint8_t)(int8_t)new_drive);
                *drive = (int8_t)new_drive;
            } else if (zone == R_WINDOW && !ctx->quiet && i % 100 == 0) {
                printf("  R-ZONE: WINDOW — 周期窗口，积极学习\n");
            }
        }

        if (ctx->rhythm.count >= 8 && i % 10 == 0) {
            pi_profile_t prof = pi_profile_from_rhythm(&ctx->rhythm);
            pidx_add(&prof, inp->tick, (uint8_t)inp->hw_stress);
            pi_match_t m = pidx_query(&prof, 0.6f);
            if (m.slot_idx >= 0 && !ctx->quiet)
                printf("  PIDX: rhythm match sim=%.3f cat=%u ref=%u\n",
                       m.similarity, m.category, m.ref_id);
        }
    }

    prev_pi_val = pi_val;
}

/* ── Branch fork + advance ── */
static void tick_branch(sched_ctx_t *ctx) {
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    {
        fork_request_t fork_req;
        fork_req.should_fork = 0;
        fork_req.current_tick = inp->tick;
        fork_req.gen_count = soul_gen_count(soul);
        fork_req.drive = drive;
        fork_req.sandbox_level = inp->mode;
        fork_req.branch_cool_tick = br_last_fork_tick();

        if (!ctx->rhythm_inited) pi_rhythm_init(&ctx->rhythm);
        float dissonance = pi_rhythm_dissonance(&ctx->rhythm);
        if (br_should_fork(&fork_req, dissonance)) {
            int slot = br_fork(soul, inp->tick, soul_gen_count(soul));
            if (slot >= 0)
                printf("[%4d] tick=%-6u BRANCH: fork created at slot %d (drive=%d)\n",
                       i, inp->tick, slot, drive);
        }
    }
    br_advance_all();
}

/* ── Idle check (every 100) ── */
static void tick_idle(sched_ctx_t *ctx) {
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
static void tick_feedback(sched_ctx_t *ctx) {
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

    /* Per-tick services */
    tick_services(ctx);

    /* Code reading (every 200) */
    if (i % 200 == 0) tick_code_read(ctx);

    /* Code modification — TLN 可以否决 */
    if (i % mod_cycle == 0) {
        if (!ctx->tln_enabled || ctx->tln_modify_hint != -1)
            tick_code_modify(ctx);
    }

    /* Code optimization */
    if (i % opt_cycle == 0) tick_code_optimize(ctx);

    /* NOP deletion */
    if (i % nop_cycle == 0) tick_nop_delete(ctx);

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

    /* TLN 进化: 每 2000 tick 做一次微小变异 (0.5% 权重) */
    if (ctx->tln_enabled && i > 0 && i % 2000 == 0) {
        int m = tln_mutate(&ctx->tln, 0.005f);
        if (m > 0 && !ctx->quiet)
            printf("[%4d] tick=%-6u TLN: evolved %d weights\n", i, inp->tick, m);
    }

    /* Print state */
    tick_print(ctx);

    inp->branch_active_count = br_active_count();
}
