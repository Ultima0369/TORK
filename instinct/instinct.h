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
    int restored_files;                /* count of files restored by ps_restore_all */
    int save_success;                  /* 1 if ps_save_all just succeeded */
    int idle_discoveries;              /* count of idle discoveries in last cycle */
    int branch_active_count;             /* How many branches are currently alive */
    int branch_fork_ticks_ago;           /* Ticks since last fork (-1=never) */
    int branch_reap_just_happened;       /* 1 if a branch was reaped this round */
    int pattern_best_action;             /* Pattern-recommended action (-1=none) */
    float pattern_confidence;            /* Confidence in pattern recommendation (0..1) */
} instinct_input_t;

tork_instinct_t instinct_evaluate(const instinct_input_t *in);

void instinct_print(int round, uint32_t tick, const tork_instinct_t *inst);

#endif
