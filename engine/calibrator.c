#include "calibrator.h"
#include "blackboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* ── Shared memory ──────────────────────────────────────────────── */
static volatile uint8_t *pmem = NULL;
static struct tork_params live_params;

/* ── Default initial values ─────────────────────────────────────── */
static const struct tork_params DEFAULTS = {
    .temp_warn                = 70,
    .temp_moderate            = 80,
    .temp_critical            = 85,
    .temp_recover_from_moderate = 75,
    .temp_recover_from_critical = 82,
    .fear_weight              = 100,
    .desire_weight            = 100,
    .curiosity_weight         = 100,
    .conservative_cycle       = 30,
    .aggressive_cycle         = 60,
    .nop_cycle                = 90,
    ._reserved                = {0},
    .checksum                 = 0,
};

/* ── Pattern statistics (filled by cal_extract_patterns) ────────── */
static struct {
    int opt_success_below_50;
    int opt_total_below_50;
    int opt_success_50_70;
    int opt_total_50_70;
    int opt_success_above_70;
    int opt_total_above_70;
    int opt_success_high_desire;
    int opt_total_high_desire;
    int opt_success_high_curiosity;
    int opt_total_high_curiosity;
    int opt_fail_high_fear;
    int opt_total_high_fear;
    int consecutive_successes;
    int consecutive_failures;
} patterns;

/* ── CRC32 ──────────────────────────────────────────────────────── */
static uint32_t crc32_calc(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
    }
    return ~crc;
}

static void compute_checksum(struct tork_params *p) {
    p->checksum = 0;
    p->checksum = crc32_calc(p, sizeof(*p));
}

/* ── Init ───────────────────────────────────────────────────────── */
int cal_init(void) {
    void *addr = mmap((void *)PARAM_BASE, PARAM_SIZE,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        perror("cal_init mmap");
        return -1;
    }
    if ((uintptr_t)addr != PARAM_BASE) {
        fprintf(stderr, "cal_init: mapped at %p, expected 0x%x\n", addr, PARAM_BASE);
        munmap(addr, PARAM_SIZE);
        return -1;
    }

    pmem = (volatile uint8_t *)addr;

    uint32_t magic;
    memcpy(&magic, (const void *)(pmem + PARAM_OFF_MAGIC), 4);

    if (magic != PARAM_MAGIC) {
        /* Fresh — write defaults */
        memcpy(&live_params, &DEFAULTS, sizeof(live_params));
        compute_checksum(&live_params);
        memcpy((void *)(pmem + PARAM_OFF_DATA), &live_params, sizeof(live_params));
        uint32_t v = PARAM_MAGIC;
        memcpy((void *)(pmem + PARAM_OFF_MAGIC), &v, 4);
        v = PARAM_VERSION;
        memcpy((void *)(pmem + PARAM_OFF_VERSION), &v, 4);
    } else {
        /* Load existing params */
        memcpy(&live_params, (const void *)(pmem + PARAM_OFF_DATA), sizeof(live_params));
    }

    return 0;
}

const struct tork_params *cal_params(void) {
    return &live_params;
}

/* ── Pattern extraction ─────────────────────────────────────────── */

/* Callback context for blackboard scan */
struct scan_ctx {
    int last_tick;
    int last_type;
    int last_value;
    int consec_ok;
    int consec_fail;
};

static void bb_scan_cb(int idx, uint32_t tick, uint16_t instance,
                       uint8_t type, uint8_t value, uint32_t payload) {
    (void)idx; (void)instance;

    if (type == BB_TYPE_OPT_SUCCESS || type == BB_TYPE_OPT_FAIL) {
        /* payload = round when event happened; we use tick as proxy for temperature.
           Since we don't store temperature in blackboard entries,
           we approximate: low tick ≈ early startup ≈ cooler.
           For a real system, we'd store temp in payload. Here we use
           a simpler heuristic based on consecutive outcomes. */

        int success = (type == BB_TYPE_OPT_SUCCESS) ? 1 : 0;

        /* Track consecutive outcomes */
        if (success) {
            patterns.consecutive_successes++;
            patterns.consecutive_failures = 0;
        } else {
            patterns.consecutive_failures++;
            patterns.consecutive_successes = 0;
        }

        /* Use value field as phase indicator (1=conservative, 2=aggressive, 3=nop)
           to segment success rates by optimization type */
        int phase = value;
        if (phase == 1) {
            patterns.opt_total_below_50++;
            if (success) patterns.opt_success_below_50++;
        } else if (phase == 2) {
            patterns.opt_total_50_70++;
            if (success) patterns.opt_success_50_70++;
        } else {
            patterns.opt_total_above_70++;
            if (success) patterns.opt_success_above_70++;
        }
    }

    (void)payload; (void)tick;
}

int cal_extract_patterns(void) {
    memset(&patterns, 0, sizeof(patterns));
    bb_read_all(bb_scan_cb);
    return 0;
}

/* ── Suggestion generation ──────────────────────────────────────── */
static int step_limit(int initial_val) {
    int s = initial_val * STEP_MAX_PCT / 100;
    return (s < 1) ? 1 : s;
}

int cal_suggest_adjustments(struct tork_params *suggested) {
    memcpy(suggested, &live_params, sizeof(*suggested));

    /* Rule 1: high-phase success rate low → lower temp_warn */
    if (patterns.opt_total_above_70 >= 2) {
        float rate_above = (float)patterns.opt_success_above_70 / patterns.opt_total_above_70;
        float rate_below = 0.0f;
        if (patterns.opt_total_below_50 > 0)
            rate_below = (float)patterns.opt_success_below_50 / patterns.opt_total_below_50;

        if (rate_above < rate_below - 0.2f) {
            int step = step_limit(DEFAULTS.temp_warn);
            int new_val = (int)suggested->temp_warn - step;
            if (new_val >= TEMP_MIN) {
                suggested->temp_warn = (uint8_t)new_val;
                printf("CAL: suggesting temp_warn %d→%d (high-phase success rate low)\n",
                       live_params.temp_warn, new_val);
                return 0;
            }
        }
    }

    /* Rule 2: high-desire success rate high → increase desire_weight */
    if (patterns.opt_total_50_70 >= 2) {
        float rate_mid = (float)patterns.opt_success_50_70 / patterns.opt_total_50_70;
        float rate_low = 0.0f;
        if (patterns.opt_total_below_50 > 0)
            rate_low = (float)patterns.opt_success_below_50 / patterns.opt_total_below_50;

        if (rate_mid > rate_low + 0.2f) {
            int step = step_limit(DEFAULTS.desire_weight);
            int new_val = (int)suggested->desire_weight + step;
            if (new_val <= WEIGHT_MAX) {
                suggested->desire_weight = (uint8_t)new_val;
                printf("CAL: suggesting desire_weight %d→%d (mid-phase success rate high)\n",
                       live_params.desire_weight, new_val);
                return 0;
            }
        }
    }

    /* Rule 3: consecutive successes → shorten conservative_cycle */
    if (patterns.consecutive_successes >= 3) {
        int step = step_limit(DEFAULTS.conservative_cycle);
        int new_val = (int)suggested->conservative_cycle - step;
        if (new_val >= CYCLE_MIN) {
            suggested->conservative_cycle = (uint8_t)new_val;
            printf("CAL: suggesting conservative_cycle %d→%d (consecutive successes)\n",
                   live_params.conservative_cycle, new_val);
            return 0;
        }
    }

    /* Rule 4: consecutive failures → lengthen conservative_cycle */
    if (patterns.consecutive_failures >= 3) {
        int step = step_limit(DEFAULTS.conservative_cycle);
        int new_val = (int)suggested->conservative_cycle + step;
        if (new_val <= CYCLE_MAX) {
            suggested->conservative_cycle = (uint8_t)new_val;
            printf("CAL: suggesting conservative_cycle %d→%d (consecutive failures)\n",
                   live_params.conservative_cycle, new_val);
            return 0;
        }
    }

    printf("CAL: no adjustment needed (patterns stable)\n");
    return 1;  /* no suggestion */
}

/* ── Apply adjustments ──────────────────────────────────────────── */
int cal_apply_adjustments(const struct tork_params *suggested) {
    /* Validate each field against safety bounds */
    if (suggested->temp_warn < TEMP_MIN || suggested->temp_warn > TEMP_MAX)
        return -1;
    if (suggested->temp_moderate < TEMP_MIN || suggested->temp_moderate > TEMP_MAX)
        return -1;
    if (suggested->temp_critical < TEMP_MIN || suggested->temp_critical > TEMP_MAX)
        return -1;
    if (suggested->temp_recover_from_moderate < TEMP_MIN ||
        suggested->temp_recover_from_moderate > TEMP_MAX)
        return -1;
    if (suggested->temp_recover_from_critical < TEMP_MIN ||
        suggested->temp_recover_from_critical > TEMP_MAX)
        return -1;

    if (suggested->fear_weight < WEIGHT_MIN || suggested->fear_weight > WEIGHT_MAX)
        return -1;
    if (suggested->desire_weight < WEIGHT_MIN || suggested->desire_weight > WEIGHT_MAX)
        return -1;
    if (suggested->curiosity_weight < WEIGHT_MIN || suggested->curiosity_weight > WEIGHT_MAX)
        return -1;

    if (suggested->conservative_cycle < CYCLE_MIN || suggested->conservative_cycle > CYCLE_MAX)
        return -1;
    if (suggested->aggressive_cycle < CYCLE_MIN || suggested->aggressive_cycle > CYCLE_MAX)
        return -1;
    if (suggested->nop_cycle < CYCLE_MIN || suggested->nop_cycle > CYCLE_MAX)
        return -1;

    /* Verify that only one parameter changed (by comparing with live) */
    int diffs = 0;
    char which[64] = "";
    int old_val = 0, new_val = 0;

    #define CHECK_FIELD(field, name) \
        if (suggested->field != live_params.field) { \
            diffs++; \
            old_val = live_params.field; \
            new_val = suggested->field; \
            snprintf(which, sizeof(which), "%s", name); \
        }

    CHECK_FIELD(temp_warn, "temp_warn");
    CHECK_FIELD(temp_moderate, "temp_moderate");
    CHECK_FIELD(temp_critical, "temp_critical");
    CHECK_FIELD(temp_recover_from_moderate, "temp_recover_from_moderate");
    CHECK_FIELD(temp_recover_from_critical, "temp_recover_from_critical");
    CHECK_FIELD(fear_weight, "fear_weight");
    CHECK_FIELD(desire_weight, "desire_weight");
    CHECK_FIELD(curiosity_weight, "curiosity_weight");
    CHECK_FIELD(conservative_cycle, "conservative_cycle");
    CHECK_FIELD(aggressive_cycle, "aggressive_cycle");
    CHECK_FIELD(nop_cycle, "nop_cycle");

    if (diffs != 1)
        return -1;

    /* Apply */
    struct tork_params validated = *suggested;
    compute_checksum(&validated);
    memcpy(&live_params, &validated, sizeof(live_params));
    memcpy((void *)(pmem + PARAM_OFF_DATA), &live_params, sizeof(live_params));

    /* Record to blackboard */
    bb_write(BB_TYPE_PARAM_ADJUST, (uint8_t)diffs, (uint32_t)new_val);

    printf("CAL: adjusting %s %d→%d\n", which, old_val, new_val);
    return 0;
}

/* ── Cleanup ────────────────────────────────────────────────────── */
void cal_cleanup(void) {
    if (pmem) {
        munmap((void *)pmem, PARAM_SIZE);
        pmem = NULL;
    }
}
