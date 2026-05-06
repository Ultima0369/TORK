/*
 * Stub for self_tune — provides minimal symbols needed by instinct.o
 * so the test binary doesn't pull in the full self_tune dependency chain.
 */
#include "../src/learning/self_tune.h"
#include <string.h>

static tune_params_t g_stub_params = {
    .fear_weight      = 1.0f,
    .desire_weight    = 1.0f,
    .curiosity_weight = 1.0f,
    .learning_rate    = 0.1f,
    .heartbeat_interval = 500,
    .exploration_rate = 20
};

tune_params_t tune_get_params(void) {
    return g_stub_params;
}

void tune_init(float fear_base, float desire_base, float curiosity_base) {
    g_stub_params.fear_weight = fear_base;
    g_stub_params.desire_weight = desire_base;
    g_stub_params.curiosity_weight = curiosity_base;
}

void tune_adjust_from_patterns(void) { }
int  tune_save(void) { return 0; }
int  tune_load(void) { return 0; }
void tune_apply_tln_hints(int a, int m, int e, int en) { (void)a; (void)m; (void)e; (void)en; }
void tune_set_param(const char *name, float value) { (void)name; (void)value; }
void tune_print(void) { }
