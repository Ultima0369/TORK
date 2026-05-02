#ifndef INSTINCT_H
#define INSTINCT_H

#include <stdint.h>

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
    uint16_t code_insns;   /* instruction count from code reading */
    uint16_t code_ctrl;    /* control-flow instruction count */
    uint8_t  code_mod_success; /* 0=none, 1=success, 2=failed */
    uint8_t  code_opt_saved;   /* cumulative dead code lines deleted */
    uint8_t  code_nop_count;   /* nop insns found in last scan */
    uint8_t  fission_count;    /* number of fissions performed */
    uint16_t wins;             /* sovereignty migration wins */
    uint32_t bb_global_opts;   /* blackboard: global optimization count */
} instinct_input_t;

tork_instinct_t instinct_evaluate(const instinct_input_t *in);

void instinct_print(int round, uint32_t tick, const tork_instinct_t *inst);

#endif
