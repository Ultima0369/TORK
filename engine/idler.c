#include "idler.h"
#include "../learning/experience.h"
#include "../learning/mcts.h"
#include "../learning/pattern.h"
#include "../learning/replay.h"
#include "../learning/observer.h"
#include "code_reader.h"
#include "../code/code_modifier.h"
#include <stdio.h>
#include <string.h>

/* ── State ──────────────────────────────────────────────────── */
static int g_active = 0;
static int g_cycles = 0;

/* ── Should enter idle ─────────────────────────────────────── */
int idler_should_enter(const idler_input_t *in) {
    if (!in) return 0;
    if (in->hw_stress >= 3) return 0;
    if (in->rounds_since_mod < 300) return 0;
    if (in->drive < -10) return 0;       /* Too fearful */
    if (in->tick - in->last_bb_tick < 200) return 0; /* Recent activity */
    return 1;
}

/* ── Run one idle cycle ────────────────────────────────────── */
idler_output_t idler_cycle(const idler_input_t *in) {
    idler_output_t out;
    memset(&out, 0, sizeof(out));
    
    if (!in) return out;
    
    g_active = 1;
    g_cycles++;
    
    printf("\n── IDLE cycle #%d ─────────────────────────────────\n", g_cycles);
    printf("  state: stress=%d drive=%d gen=%u tick=%u\n",
           in->hw_stress, in->drive, in->gen_count, in->tick);
    
    /* ── Step 1: Build MCTS state ── */
    mcts_state_t mcts_state;
    memset(&mcts_state, 0, sizeof(mcts_state));
    
    mcts_state.hw_stress      = in->hw_stress;
    mcts_state.drive          = in->drive;
    mcts_state.gen_count      = in->gen_count;
    mcts_state.recent_crashes = 0; /* Engine can update this via blackboard */
    
    for (int t = 0; t < MCTS_NUM_ACTIONS; t++) {
        float sr = exp_success_rate((uint8_t)t);
        mcts_state.exp_success[t] = (sr >= 0) ? sr : 0.0f;
    }
    
    /* ── Step 2: MCTS search (500ms budget = ~5000 iterations) ── */
    printf("  MCTS: searching...\n");
    mcts_result_t result = mcts_search(&mcts_state, 500);
    mcts_print_result(&result);
    
    /* ── Step 3: Record experience for this decision ── */
    /* (Outcome will be updated later by engine after action executes) */
    exp_record(
        in->tick, in->hw_stress, in->drive, in->gen_count,
        result.action.type, result.action.param,
        0,    /* outcome — filled in later by engine */
        0,    /* crash — filled later */
        0,    /* compile_ok — filled later */
        in->hw_stress, in->drive
    );
    
    printf("  experience recorded (slot %u)\n", exp_count() - 1);
    
    /* ── Step 4: Return recommended action ── */
    out.action_type    = result.action.type;
    out.action_param   = result.action.param;
    out.expected_value = result.expected_value;
    out.discoveries    = 0;
    
        /* Auto-tune MCTS every 10 cycles */
    if (g_cycles % 10 == 0) mcts_auto_tune();
    
    /* Pattern learning: extract rules from experience */
    if (g_cycles % 3 == 0) pat_learn_from_experience();
    
    /* Deep replay: every ~5 cycles, replay past experiences with "what if" */
    if (g_cycles % 5 == 0) replay_deep();
    
    /* Update observer baseline and check for anomalies */
    if (g_cycles % 2 == 0) {
        obs_update_baseline();
        int anomaly = obs_check_anomaly(0, 0, 0, 0);
        if (anomaly > 1) {
            printf("  OBS: baseline anomaly detected in idle\n");
        }
    }
    printf("── IDLE cycle complete ────────────────────────────\n\n");
    
    return out;
}

int idler_active(void) {
    return g_active;
}

void idler_set_active(int active) {
    g_active = active;
}

int idler_cycle_count(void) {
    return g_cycles;
}

/* ── MCTS 代码修改执行器 ──────────────────────────────────────
 * 当 MCTS 决策出代码修改子动作时，通过 code_modifier 实际执行。
 * 返回: 0=成功, -1=失败/不适用
 */
int idler_mcts_modify(uint8_t action_type, const char *target_file,
                       const char *func_name) {
    if (!target_file || !func_name) return -1;

    char asm_buf[8192];
    int alen = asm_read_file(target_file, asm_buf, sizeof(asm_buf));
    if (alen <= 0) return -1;

    char backup[8192];
    memcpy(backup, asm_buf, alen);

    switch (action_type) {
    case MCTS_MOD_REPLACE_OP: {
        int rep = asm_replace_operand(asm_buf, alen, func_name,
                                      "\tje\t", "\tjz\t", 1);
        if (rep != 1) return -1;
        if (!asm_verify_modification(asm_buf, alen, "benchmark/memcpy")) {
            asm_rollback(asm_buf, sizeof(asm_buf), backup, alen);
            return -1;
        }
        FILE *f = fopen(target_file, "w");
        if (f) { fwrite(asm_buf, 1, alen, f); fclose(f); }
        printf("  MCTS-MOD: replaced je→jz (verified)\n");
        return 0;
    }
    case MCTS_MOD_DEL_DEAD: {
        int new_len = alen;
        int del = asm_delete_dead_insns(asm_buf, alen, func_name, &new_len);
        if (del <= 0) return -1;
        if (!asm_verify_modification(asm_buf, new_len, "benchmark/memcpy")) {
            asm_rollback(asm_buf, sizeof(asm_buf), backup, alen);
            return -1;
        }
        FILE *f = fopen(target_file, "w");
        if (f) { fwrite(asm_buf, 1, new_len, f); fclose(f); }
        printf("  MCTS-MOD: deleted %d dead insn(s) (verified)\n", del);
        return 0;
    }
    case MCTS_MOD_DEL_NOP: {
        int new_len = alen;
        int nops = asm_delete_nop_insns(asm_buf, alen, func_name, &new_len);
        if (nops <= 0) return -1;
        if (!asm_verify_modification(asm_buf, new_len, "benchmark/memcpy")) {
            asm_rollback(asm_buf, sizeof(asm_buf), backup, alen);
            return -1;
        }
        FILE *f = fopen(target_file, "w");
        if (f) { fwrite(asm_buf, 1, new_len, f); fclose(f); }
        printf("  MCTS-MOD: deleted %d nop(s) (verified)\n", nops);
        return 0;
    }
    case MCTS_MOD_SWAP_REGS: {
        /* 寄存器交换: rax↔rdx 在 mov 指令中 */
        int rep = asm_replace_operand(asm_buf, alen, func_name,
                                      "%rax", "%rdx", 1);
        if (rep != 1) return -1;
        if (!asm_verify_modification(asm_buf, alen, "benchmark/memcpy")) {
            asm_rollback(asm_buf, sizeof(asm_buf), backup, alen);
            return -1;
        }
        FILE *f = fopen(target_file, "w");
        if (f) { fwrite(asm_buf, 1, alen, f); fclose(f); }
        printf("  MCTS-MOD: swapped rax↔rdx (verified)\n");
        return 0;
    }
    default:
        return -1;
    }
}
