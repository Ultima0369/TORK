#include "instinct.h"
#include "../learning/self_tune.h"
#include "calibrator.h"
#include <stdio.h>

/* ── 本能微调系数 (命名常量，不再硬编码) ──────────────────── */

/* fear: 压力等级 → 恐惧基数 */
#define FEAR_HIGH     1.0f    /* hw_stress >= 3 */
#define FEAR_MED      0.6f    /* hw_stress >= 2 */
#define FEAR_LOW      0.3f    /* hw_stress >= 1 */

/* desire: 进展 → 欲望基数 */
#define DESIRE_MOD    0.8f    /* code_mod_success */
#define DESIRE_OPT    0.5f    /* code_opt_saved */
#define DESIRE_SLOW   0.3f    /* elapsed > expected */

/* curiosity: 探索 → 好奇基数 */
#define CURI_RATIO    0.5f    /* ctrl/insns ratio 系数 */
#define CURI_NOP      0.2f    /* NOP awareness */

/* 微调增量 (不乘权重) */
#define BONUS_WIN         0.3f    /* sovereignty win */
#define BONUS_BB_OPT      0.1f    /* blackboard 全局优化 */
#define BONUS_RULE        0.3f    /* 活跃归纳规则 */
#define BONUS_RULE_APPLY  0.2f    /* 规则被成功应用 */
#define BONUS_SAVE        0.05f   /* 持久化成功 */
#define BONUS_IDLE_DISC   0.2f    /* 闲时发现 */
#define PENAL_IDLE_REST   0.05f   /* 闲时=休息，减欲望 */
#define BONUS_GEN_CURI    0.08f   /* 世代积累好奇 */
#define BONUS_CLOUD       0.12f   /* 云协作好奇 */
#define BONUS_REAP        0.1f    /* 分支收割 */
#define BONUS_FORK_RECENT 0.1f    /* 最近 fork */
#define BONUS_ENV_CURI    0.35f   /* 环境变化好奇 */
#define BONUS_ENV_DESIRE  0.1f    /* 环境变化欲望 */
#define BONUS_PAT_DESIRE  0.15f   /* 模式匹配欲望 */
#define BONUS_PAT_CURI_H  0.2f    /* 高置信模式好奇 */
#define BONUS_PAT_CURI_L  0.1f    /* 低置信模式好奇 */

/* 乘法微调 (0-1 范围) */
#define MULT_RESTORE_FEAR  0.9f   /* 恢复文件减轻恐惧 */
#define MULT_BRANCH_FEAR   0.95f  /* 分支减轻恐惧 */
#define MULT_THROTTLE_CUR  0.7f   /* 节流好奇衰减 */
#define MULT_THROTTLE_DES  0.8f   /* 节流欲望衰减 */
#define MULT_THROTTLE_FEAR 1.2f   /* 节流恐惧增加 */
#define MULT_PERF_CUR      1.3f   /* 性能模式好奇增幅 */
#define MULT_PERF_DES      1.2f   /* 性能模式欲望增幅 */
#define MULT_HEAVY_CUR     0.8f   /* 重节流好奇衰减 */
#define MULT_PAT_FEAR_H    0.92f  /* 高置信模式减轻恐惧 */
#define MULT_PAT_FEAR_L    1.05f  /* 低置信模式增加恐惧 */
#define MULT_ENV_FEAR      0.9f   /* 环境变化减轻恐惧 */

/* 阈值 */
#define PAT_CONF_HIGH  0.6f
#define PAT_CONF_LOW   0.3f
#define THROTTLE_HEAVY 0.5f
#define FORK_RECENT    100

static struct tork_params fallback_params = {
    .temp_warn                = 70,
    .temp_moderate            = 80,
    .temp_critical            = 85,
    .temp_recover_from_moderate = 75,
    .temp_recover_from_critical = 82,
    .fear_weight              = 100,
    .desire_weight            = 100,
    .curiosity_weight         = 100,
    .conservative_cycle       = 30,
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
        inst.fear = FEAR_HIGH * fw;
    else if (in->hw_stress >= 2)
        inst.fear = FEAR_MED * fw;
    else if (in->hw_stress >= 1)
        inst.fear = FEAR_LOW * fw;

    /* ── desire: progress-driven ───────────────────────────── */
    if (in->code_mod_success == 1)
        inst.desire = DESIRE_MOD * dw;
    else if (in->code_opt_saved > 0)
        inst.desire = DESIRE_OPT * dw;
    else if (in->elapsed > in->expected)
        inst.desire = DESIRE_SLOW * dw;

    /* ── curiosity: exploration-driven ─────────────────────── */
    if (in->code_insns > 0 && in->code_ctrl > 0) {
        float ratio = (float)in->code_ctrl / in->code_insns;
        inst.curiosity = ratio * CURI_RATIO * cw;
    }

    /* ── NOP awareness ─────── */
    if (in->code_nop_count > 0)
        inst.curiosity += CURI_NOP * cw;

    /* ── sovereignty win feedback ──────────────────────────── */
    if (in->wins > 0)
        inst.desire += BONUS_WIN;

    /* ── blackboard: global optimization awareness ────────── */
    if (in->bb_global_opts > 0)
        inst.curiosity += BONUS_BB_OPT;

    /* ── inductive rule awareness ─────────────────────────── */
    if (in->active_rules > 0)
        inst.curiosity += BONUS_RULE;

    if (in->rule_applied)
        inst.desire += BONUS_RULE_APPLY;

    /* ── persistence microadjustments ─────────────────────── */
    if (in->restored_files > 0)
        inst.fear *= MULT_RESTORE_FEAR;

    if (in->save_success)
        inst.desire += BONUS_SAVE;

    /* ── idle microadjustments ──────────────────────────── */
    if (in->idle_discoveries > 0) {
        inst.curiosity += BONUS_IDLE_DISC;
        inst.desire -= PENAL_IDLE_REST;
    } else if (in->idle_discoveries == IDLE_ENDED_NONE) {
        inst.desire -= PENAL_IDLE_REST;
    }

    /* ── generation-aware curiosity ── */
    if (in->code_opt_saved > 3 && in->active_rules > 0)
        inst.curiosity += BONUS_GEN_CURI * cw;

    /* ── cloud collaboration awareness ── */
    if (in->code_mod_success == 1)
        inst.curiosity += BONUS_CLOUD * cw;

    /* ── branch awareness ── */
    if (in->branch_active_count > 0) {
        inst.curiosity += BONUS_REAP * cw;
        inst.fear *= MULT_BRANCH_FEAR;
    }
    if (in->branch_reap_just_happened)
        inst.desire += BONUS_REAP * dw;
    if (in->branch_fork_ticks_ago >= 0 && in->branch_fork_ticks_ago < FORK_RECENT)
        inst.curiosity += BONUS_FORK_RECENT * cw;

    /* ── energy awareness ── */
    if (in->energy_mode == 2 || in->energy_mode == 3) {
        inst.curiosity *= MULT_THROTTLE_CUR;
        inst.fear *= MULT_THROTTLE_FEAR;
        inst.desire *= MULT_THROTTLE_DES;
    } else if (in->energy_mode == 0) {
        inst.curiosity *= MULT_PERF_CUR;
        inst.desire *= MULT_PERF_DES;
    }

    if (in->energy_throttle > THROTTLE_HEAVY)
        inst.curiosity *= MULT_HEAVY_CUR;

    /* ── pattern experience awareness ── */
    if (in->pattern_best_action >= 0 && in->pattern_confidence > PAT_CONF_LOW) {
        inst.fear *= MULT_PAT_FEAR_H;
        inst.desire += BONUS_PAT_DESIRE * dw;
        if (in->pattern_confidence > PAT_CONF_HIGH)
            inst.curiosity += BONUS_PAT_CURI_H * cw;
    } else if (in->pattern_best_action >= 0 && in->pattern_confidence > 0.0f) {
        inst.fear *= MULT_PAT_FEAR_L;
        inst.curiosity += BONUS_PAT_CURI_L * cw;
    }

    /* ── 环境变化：僵死打破 ── */
    if (in->env_changed) {
        inst.curiosity += BONUS_ENV_CURI * cw;
        inst.fear *= MULT_ENV_FEAR;
        inst.desire += BONUS_ENV_DESIRE * dw;
    }

    /* ── 社交本能：同类感知 ── */
    if (in->peer_count > 0) {
        inst.fear *= 0.95f;          /* 群聚减轻恐惧 */
        inst.curiosity += 0.05f;     /* 同类刺激好奇 */
        if (in->peer_count >= 3)
            inst.desire += 0.08f;    /* 竞争意识：同类多则欲望升 */
    }

    // TORK_EVOLVE: instinct_return_before
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