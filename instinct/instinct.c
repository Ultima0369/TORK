#include "instinct.h"
#include "calibrator.h"
#include <stdio.h>

static const struct tork_params fallback_params = {
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

    return inst;
}

void instinct_print(int round, uint32_t tick, const tork_instinct_t *inst) {
    printf("[%4d] tick=%-6u fear=%.2f desire=%.2f curiosity=%.2f\n",
           round, tick, inst->fear, inst->desire, inst->curiosity);
}
