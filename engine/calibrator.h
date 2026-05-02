#ifndef CALIBRATOR_H
#define CALIBRATOR_H

#include <stdint.h>

/* Parameter shared memory at 0x301000 (one page after blackboard) */
#define PARAM_BASE      0x301000
#define PARAM_SIZE      4096
#define PARAM_MAGIC     0x43414C00  /* "CAL\0" */
#define PARAM_VERSION   1

/* Offsets within param page */
#define PARAM_OFF_MAGIC    0x000
#define PARAM_OFF_VERSION  0x004
#define PARAM_OFF_DATA     0x008

/* Hard safety bounds — cannot be overridden by calibration */
#define TEMP_MIN           30
#define TEMP_MAX           95
#define WEIGHT_MIN         10
#define WEIGHT_MAX         300
#define CYCLE_MIN          5     /* stored as /10, so 5 = 50 rounds */
#define CYCLE_MAX          300   /* stored as /10, so 300 = 3000 rounds */
#define STEP_MAX_PCT       10    /* max step = 10% of initial value */

struct tork_params {
    /* Temperature thresholds (Celsius) */
    uint8_t  temp_warn;                    /* default 70 */
    uint8_t  temp_moderate;                /* default 80 */
    uint8_t  temp_critical;                /* default 85 */

    /* Hysteresis recovery thresholds (Celsius) */
    uint8_t  temp_recover_from_moderate;   /* default 75 */
    uint8_t  temp_recover_from_critical;   /* default 82 */

    /* Instinct weights (x100, so 100 = 1.0) */
    uint8_t  fear_weight;                  /* default 100 */
    uint8_t  desire_weight;                /* default 100 */
    uint8_t  curiosity_weight;             /* default 100 */

    /* Optimization cycles (divided by 10, so 30 = 300 rounds) */
    uint8_t  conservative_cycle;           /* default 30 */
    uint8_t  aggressive_cycle;             /* default 60 */
    uint8_t  nop_cycle;                    /* default 90 */

    uint8_t  _reserved[3];                /* padding to align checksum */

    /* Checksum */
    uint32_t checksum;                     /* CRC32 of fields above */
} __attribute__((packed));

/* Initialize parameter shared memory. Returns 0 on success. */
int cal_init(void);

/* Get pointer to current params (read-only view). */
const struct tork_params *cal_params(void);

/* Scan blackboard, extract statistical patterns. Returns 0. */
int cal_extract_patterns(void);

/* Generate one parameter adjustment suggestion. Returns 0 if suggestion made. */
int cal_suggest_adjustments(struct tork_params *suggested);

/* Apply a suggested adjustment after validation. Returns 0 on success, -1 rejected. */
int cal_apply_adjustments(const struct tork_params *suggested);

/* Cleanup shared memory. */
void cal_cleanup(void);

#endif
