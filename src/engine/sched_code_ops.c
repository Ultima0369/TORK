#include "sched_code_ops.h"
#include "soul_access.h"
#include "code_reader.h"
#include "code_modifier.h"
#include "blackboard.h"
#include "../learning/experience.h"
#include "../learning/pattern.h"
#include "../persist/code_archive.h"
#include "../code/strict_verifier.h"
#include <stdio.h>
#include <string.h>

/* ── Code reading (every 200) ── */
void tick_code_read(sched_ctx_t *ctx) {
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
    uint16_t _v;
    _v = (uint16_t)insns; memcpy(stats + 0, &_v, 2);
    _v = (uint16_t)cm;    memcpy(stats + 2, &_v, 2);
    _v = (uint16_t)ca;    memcpy(stats + 4, &_v, 2);
    _v = (uint16_t)cc;    memcpy(stats + 6, &_v, 2);
    _v = (uint16_t)co;    memcpy(stats + 8, &_v, 2);
    soul_write_buf(soul, S_CODE_INSNS, stats, 10);
    inp->code_insns = (uint16_t)insns;
    inp->code_ctrl  = (uint16_t)cc;
}

/* ── Code modification: je→jz (mod_cycle) ── */
void tick_code_modify(sched_ctx_t *ctx) {
    // TORK_EVOLVE: engine_rounds_insert
    if (ctx->mod_attempted) return;
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    /* CODE-4: 查询当前情境的 top 模式，指导修改决策 */
    {
        pat_entry_t top_pats[3];
        int n = pat_query_top_survival(3, top_pats);
        if (n > 0 && inp->hw_stress >= 2) {
            /* 高压下查询分类模式：如果 top 模式建议避免修改，跳过 */
            pat_entry_t cat_pats[3];
            int cn = pat_query_by_category(0, 3, cat_pats);
            for (int pi = 0; pi < cn; pi++) {
                if (cat_pats[pi].survival_ticks > 5000) {
                    printf("[%4d] tick=%-6u MODIFY: pattern advises caution (survival=%u), skipping\n",
                           i, inp->tick, cat_pats[pi].survival_ticks);
                    ctx->mod_attempted = 1;
                    return;
                }
            }
        }
        if (n > 0 && !ctx->quiet)
            printf("[%4d] tick=%-6u MODIFY: top pattern survival=%u\n",
                   i, inp->tick, top_pats[0].survival_ticks);
    }

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
            ca_record(0, 1, 0, "je→jz success");
        } else {
            asm_rollback(asm_buf, sizeof(asm_buf), backup, backup_len);
            printf("[%4d] tick=%-6u MODIFY FAILED: je→jz rejected\n", i, inp->tick);
            uint8_t ms = 2;
            exp_update_last(-30, 0, 0, inp->hw_stress, drive);
            soul_write_buf(soul, S_CODE_MOD_SUCCESS, &ms, 1);
            inp->code_mod_success = 2;
            bb_write(BB_TYPE_OPT_FAIL, 1, (uint32_t)i);
            ca_mark_dead(0, CA_DEATH_COMPILE_FAIL, 0);
            sv_record_result(0, 0, (uint32_t)i);
            {
                int16_t worst = soul_worst_outcome(soul);
                if (-30 < worst) {
                    int16_t new_worst = -30;
                    soul_write_buf(soul, S_WORST_OUTCOME, &new_worst, 2);
                }
            }
        }
    } else {
        printf("[%4d] tick=%-6u MODIFY SKIP: je not found\n", i, inp->tick);
    }
    ctx->mod_attempted = 1;
}

/* ── Dead code optimization (opt_cycle) ── */
void tick_code_optimize(sched_ctx_t *ctx) {
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
            {
                int16_t worst = soul_worst_outcome(soul);
                if (-20 < worst) {
                    int16_t new_worst = -20;
                    soul_write_buf(soul, S_WORST_OUTCOME, &new_worst, 2);
                }
            }
        }
    } else {
        printf("[%4d] tick=%-6u OPT: no dead code found\n", i, inp->tick);
    }
    ctx->opt_attempted = 1;
}

/* ── NOP deletion (nop_cycle) ── */
void tick_nop_delete(sched_ctx_t *ctx) {
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
            {
                int16_t worst = soul_worst_outcome(soul);
                if (-20 < worst) {
                    int16_t new_worst = -20;
                    soul_write_buf(soul, S_WORST_OUTCOME, &new_worst, 2);
                }
            }
        }
    } else {
        printf("[%4d] tick=%-6u NOP: no nop insns found in memcpy_tork\n", i, inp->tick);
        uint8_t nc = 0;
        soul_write_buf(soul, S_CODE_NOP_COUNT, &nc, 1);
        inp->code_nop_count = 0;
    }
    ctx->nop_attempted = 1;
}
