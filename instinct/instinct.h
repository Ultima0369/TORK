#ifndef INSTINCT_H
#define INSTINCT_H

#include <stdint.h>

struct tork_params;

typedef struct {
    float fear;
    float desire;
    float curiosity;
} tork_instinct_t;

typedef struct {
    uint32_t tick;
    uint64_t elapsed;
    uint64_t expected;
    uint8_t  hw_stress;
    uint8_t  mode;
    uint16_t code_insns;
    uint16_t code_ctrl;
    uint8_t  code_mod_success;
    uint8_t  code_opt_saved;
    uint8_t  code_nop_count;
    uint8_t  fission_count;
    uint16_t wins;
    uint32_t bb_global_opts;
    const struct tork_params *params;  /* calibrator params, may be NULL */
    int active_rules;                  /* count of active inductive rules */
    int rule_applied;                  /* 1 if a rule was just applied successfully */
} instinct_input_t;

tork_instinct_t instinct_evaluate(const instinct_input_t *in);

void instinct_print(int round, uint32_t tick, const tork_instinct_t *inst);

#endif
