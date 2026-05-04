#include "instinct.h"
#include "../learning/self_tune.h"
#include "calibrator.h"
#include <stdio.h>

static struct tork_params fallback_params = {
    .temp_warn                = 70,
    .temp_moderate            = 80,
    .temp_critical            = 85,
    .temp_recover_from_moderate = 75,
    .temp_recover_from_critical = 82,
    .fear_weight              = 100,
    .desire_weight            = 70,
    .curiosity_weight         = 115,
    .conservative_cycle       = 25,
    .aggressive_cycle         = 60,
    .nop_cycle                = 90,
    ._reserved                = {0},
    .checksum                 = 0,
};

static const struct tork_params *get_params(const instinct_input_t *in) {
    return in->params ? in->params : &fallback_params;
}

tork_instinct_t instinct_evaluate(const instinct_input_t *in) {
    tork_instinct_t inst = {0.0f, 0.0f, 0.0f};
    const struct tork_params *p = get_params(in);

    float fw = p->fear_weight / 100.0f;
    float dw = p->desire_weight / 100.0f;
    float cw = p->curiosity_weight / 100.0f;

    /* ── fear: temperature-driven ──────────────────────────── */
    if (in->hw_stress >= 3)
        inst.fear = 1.0f * fw;
    else if (in->hw_stress >= 2)
        inst.fear = 0.6f * fw;
    else if (in->hw_stress >= 1)
        inst.fear = 0.3f * fw;

    /* ── desire: progress-driven ───────────────────────────── */
    if (in->code_mod_success == 1)
        inst.desire = 0.8f * dw;
    else if (in->code_opt_saved > 0)
        inst.desire = 0.5f * dw;
    else if (in->elapsed > in->expected)
        inst.desire = 0.3f * dw;

    /* ── curiosity: exploration-driven ─────────────────────── */
    if (in->code_insns > 0 && in->code_ctrl > 0) {
        float ratio = (float)in->code_ctrl / in->code_insns;
        inst.curiosity = ratio * 0.5f * cw;
    }

    /* ── NOP awareness: curiosity boost from nop count ─────── */
    if (in->code_nop_count > 0)
        inst.curiosity += 0.2f * cw;

    /* ── sovereignty win feedback ──────────────────────────── */
    if (in->wins > 0)
        inst.desire += 0.3f;

    /* ── blackboard: global optimization awareness ────────── */
    if (in->bb_global_opts > 0)
        inst.curiosity += 0.1f;

    /* ── inductive rule awareness ─────────────────────────── */
    if (in->active_rules > 0)
        inst.curiosity += 0.3f;    /* new abstract knowledge is worth exploring */

    if (in->rule_applied)
        inst.desire += 0.2f;       /* rule generalization success = positive feedback */

    /* ── persistence microadjustments ─────────────────────── */
    if (in->restored_files > 0)
        inst.fear *= 0.9f;        /* memory continuity reduces fear */

    if (in->save_success)
        inst.desire += 0.05f;     /* successful save boosts desire */

    /* ── idle microadjustments ──────────────────────────── */
    if (in->idle_discoveries > 0) {
        inst.curiosity += 0.2f;   /* idle discoveries feed curiosity */
        inst.desire -= 0.05f;     /* idle is rest, not ambition */
    } else if (in->idle_discoveries == -1) {
        inst.desire -= 0.05f;     /* idle ended with no discoveries */
    }

    /* ── v2.2: generation-aware curiosity ── */
    if (in->code_opt_saved > 3 && in->active_rules > 0)
        inst.curiosity += 0.08f * cw;  /* accumulated knowledge fuels exploration */

    /* ── v2.2: cloud collaboration awareness ── */
    if (in->code_mod_success == 1)
        inst.curiosity += 0.12f * cw;
    
    /* ── v3.1: branch awareness ── */
    if (in->branch_active_count > 0) {
        inst.curiosity += 0.15f * cw;  /* Active branches spark curiosity */
        inst.fear *= 0.95f;            /* Branching reduces fear (safety in diversity) */
    }
    if (in->branch_reap_just_happened) {
        inst.desire += 0.1f * dw;      /* Reaping is learning, boosts desire */
    }
    if (in->branch_fork_ticks_ago >= 0 && in->branch_fork_ticks_ago < 100) {
        inst.curiosity += 0.1f * cw;   /* Recent fork keeps mind open */
    }
    
    /* ── v3.3: energy awareness ── */
    /* High throttle means conserve energy - reduce curiosity, increase caution */
    if (in->energy_mode == 2 || in->energy_mode == 3) {
        inst.curiosity *= 0.7f;         /* Save energy, less exploring */
        inst.fear *= 1.2f;              /* More cautious when throttled */
        inst.desire *= 0.8f;            /* Lower ambition */
    } else if (in->energy_mode == 0) {
        inst.curiosity *= 1.3f;         /* Performance mode: more curious */
        inst.desire *= 1.2f;            /* More ambitious */
    }
    
    /* Heavy throttle reduces heartbeat ambition */
    if (in->energy_throttle > 0.5f) {
        inst.curiosity *= 0.8f;
    }
    
    /* ── v3.2: pattern experience awareness ── */
    if (in->pattern_best_action >= 0 && in->pattern_confidence > 0.3f) {
        /* Pattern says there's a known-good action for this situation */
        inst.fear *= 0.92f;            /* Experience reduces fear */
        inst.desire += 0.15f * dw;     /* Pattern match boosts desire (confidence) */
        if (in->pattern_confidence > 0.6f) {
            inst.curiosity += 0.2f * cw;  /* Strong pattern sparks focused curiosity */
        }
    } else if (in->pattern_best_action >= 0 && in->pattern_confidence > 0.0f) {
        /* Low confidence: be cautious, but still curious */
        inst.fear *= 1.05f;            /* Unknown territory, slight caution */
        inst.curiosity += 0.1f * cw;   /* Still worth exploring */
    }

    /* ── 时效性：环境变了但驱力没变 = 僵死，必须打破 ──
     * "世界变了,TORK必须变"
     * 环境在变（stress 波动、tick 推进）但 drive 停滞，
     * 说明 TORK 对世界的变化视而不见——这是最深的伪确定。
     * 强制注入好奇，拒绝僵死。
     */
    if (in->env_changed) {
        inst.curiosity += 0.35f * cw;   /* 环境变了，必须好奇 */
        inst.fear *= 0.9f;              /* 僵死时恐惧反而不重要 */
        inst.desire += 0.1f * dw;       /* 世界在动，你也该动 */
    }

    return inst;
}


/* ── 从自调参模块更新权重 ── */
void instinct_apply_tune(void) {
    tune_params_t tp = tune_get_params();
    fallback_params.fear_weight      = (int)(tp.fear_weight * 100);
    fallback_params.desire_weight    = (int)(tp.desire_weight * 100);
    fallback_params.curiosity_weight = (int)(tp.curiosity_weight * 100);
    printf("  INST: applied tuned params fear=%d desire=%d curiosity=%d\n",
           fallback_params.fear_weight, fallback_params.desire_weight,
           fallback_params.curiosity_weight);
}

void instinct_print(int round, uint32_t tick, const tork_instinct_t *inst) {
    printf("[%4d] tick=%-6u fear=%.2f desire=%.2f curiosity=%.2f\n",
           round, tick, inst->fear, inst->desire, inst->curiosity);
}

/* TORK EVO 20260504_1109: instinct_params */
