#ifndef IDLER_H
#define IDLER_H

#include <stdint.h>

/* Check whether idle conditions are met. Returns 1 if should enter idle. */
int idler_should_enter(uint8_t hw_stress, float fear, float desire,
                       int rounds_since_mod, uint32_t current_tick,
                       uint32_t last_bb_tick);

/* Run one idle cycle: replay → associate → discover. Returns discoveries count. */
int idler_cycle(void);

/* Returns 1 if currently in idle mode. */
int idler_active(void);

/* Set idle state (called by engine on enter/exit). */
void idler_set_active(int active);

/* Get count of idle discoveries in last cycle. */
int idler_last_discoveries(void);

#endif