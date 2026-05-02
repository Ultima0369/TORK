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
} instinct_input_t;

tork_instinct_t instinct_evaluate(const instinct_input_t *in);

void instinct_print(int round, uint32_t tick, const tork_instinct_t *inst);

#endif
