#include "instinct.h"
#include <stdio.h>
#include <math.h>

tork_instinct_t instinct_evaluate(const instinct_input_t *in) {
    tork_instinct_t inst = {0.0f, 0.0f, 0.0f};

    /* ── fear ─────────────────────────────────────────────── */
    if (in->hw_stress >= 3)
        inst.fear = 1.0f;
    else if (in->hw_stress == 2)
        inst.fear = 0.6f;
    else if (in->hw_stress == 1)
        inst.fear = 0.3f;
    else {
        if (in->expected > 0) {
            double dev = fabs((double)in->elapsed - (double)in->expected)
                         / (double)in->expected;
            if (dev > 0.20)
                inst.fear = 0.2f;
        }
    }

    /* ── desire ───────────────────────────────────────────── */
    if (in->hw_stress >= 2) {
        inst.desire = 0.0f;
    } else if (in->expected > 0) {
        double dev = fabs((double)in->elapsed - (double)in->expected)
                     / (double)in->expected;
        if (dev < 0.05)
            inst.desire = 0.6f;
        else if (dev < 0.15)
            inst.desire = 0.3f;
        else
            inst.desire = 0.0f;
    }

    /* ── curiosity ────────────────────────────────────────── */
    if (in->hw_stress == 0 && inst.fear < 0.3f && inst.desire > 0.3f)
        inst.curiosity = 0.4f;
    else if (in->hw_stress == 0 && inst.fear < 0.3f)
        inst.curiosity = 0.2f;
    else
        inst.curiosity = 0.0f;

    /* ── code-awareness adjustments ──────────────────────── */
    if (in->code_insns > 0) {
        inst.curiosity += 0.1f;
        if (in->code_ctrl * 100 / in->code_insns > 30)
            inst.desire += 0.1f;
    }

    /* ── code modification feedback ──────────────────────── */
    if (in->code_mod_success == 1)
        inst.desire += 0.2f;
    else if (in->code_mod_success == 2)
        inst.fear += 0.1f;

    /* ── code optimization feedback ──────────────────────── */
    if (in->code_opt_saved > 0)
        inst.desire += 0.3f;

    return inst;
}

void instinct_print(int round, uint32_t tick, const tork_instinct_t *inst) {
    printf("[%4d] tick=%-6u fear=%.1f desire=%.1f curiosity=%.1f\n",
           round, tick, inst->fear, inst->desire, inst->curiosity);
}