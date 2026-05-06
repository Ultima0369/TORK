#include "sched_inductive.h"
#include "inductor.h"

/* ── Inductive learning (every 800) ── */
void tick_inductive(sched_ctx_t *ctx) {
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
void tick_inductive_test(sched_ctx_t *ctx) {
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
void tick_inductive_apply(sched_ctx_t *ctx) {
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
