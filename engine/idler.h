#ifndef IDLER_H
#define IDLER_H

#include <stdint.h>

/* ── Idle Learning Cycle ─────────────────────────────────────
 *  TORK's "rest and digest" mode. When the main loop is quiet,
 *  idler runs MCTS-based decision search, records experiences,
 *  and returns recommended actions for the engine to apply.
 * ──────────────────────────────────────────────────────────── */

/* State snapshot passed from engine to idler */
typedef struct {
    uint8_t  hw_stress;
    int8_t   drive;
    uint16_t gen_count;
    uint32_t tick;
    uint32_t last_bb_tick;
    int      rounds_since_mod;
} idler_input_t;

/* Decision output from idler back to engine */
typedef struct {
    uint8_t  action_type;    /* 0=adjust_fear, 1=curiosity, 2=heartbeat,
                                3=modify, 4=optimize, 5=pattern_analyze, 6=call_cloud */
    int8_t   action_param;
    float    expected_value;
    int      discoveries;    /* Patterns / improvements found */
} idler_output_t;

/* Check whether idle conditions are met */
int idler_should_enter(const idler_input_t *in);

/* Run one full idle cycle: MCTS → simulate → record → output */
idler_output_t idler_cycle(const idler_input_t *in);

/* Returns 1 if currently in idle mode */
int idler_active(void);

/* Set idle state */
void idler_set_active(int active);

/* Total number of idle cycles completed */
int idler_cycle_count(void);

#endif /* IDLER_H */
