#include "idler.h"
#include "blackboard.h"
#include "inductor.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── State ──────────────────────────────────────────────────────── */
static int g_active = 0;
static int g_last_discoveries = 0;

/* ── Blackboard event snapshot for replay ───────────────────────── */
#define IDLE_MAX_EVENTS 512

struct bb_event {
    uint32_t tick;
    uint8_t  type;
    uint8_t  value;
    uint32_t payload;
};

static struct bb_event g_events[IDLE_MAX_EVENTS];
static int g_event_count = 0;

/* ── Co-occurrence matrix ───────────────────────────────────────── */
/* Rows: outcomes (opt_ok, opt_fail, fission_win)
   Cols: conditions (temp_high, temp_low, desire_high, desire_low, fear_high) */
#define ROWS 3
#define COLS 5

static const char *row_labels[ROWS] = {"opt_ok", "opt_fail", "fission_w"};
static const char *col_labels[COLS] = {"temp_hi", "temp_lo", "desire_h", "desire_l", "fear_h"};

static int g_matrix[ROWS][COLS];

static uint32_t g_replay_start_tick;
static uint32_t g_replay_end_tick;

/* ── Callbacks ─────────────────────────────────────────────────── */

/* Collect events for replay (last ~5000 ticks worth) */
static void collect_events_cb(int idx, uint32_t tick, uint16_t instance,
                               uint8_t type, uint8_t value, uint32_t payload) {
    (void)idx; (void)instance;
    if (g_event_count >= IDLE_MAX_EVENTS) return;

    /* Only collect events within replay window */
    if (tick >= g_replay_start_tick && tick <= g_replay_end_tick) {
        struct bb_event *e = &g_events[g_event_count++];
        e->tick = tick;
        e->type = type;
        e->value = value;
        e->payload = payload;
    }
}

/* ── Should enter idle ─────────────────────────────────────────── */
int idler_should_enter(uint8_t hw_stress, float fear, float desire,
                       int rounds_since_mod, uint32_t current_tick,
                       uint32_t last_bb_tick) {
    /* 1. hw_stress < 3 means not critical (allow moderate stress for idle) */
    if (hw_stress >= 3) return 0;

    /* 2. No code modification in last 300 rounds */
    if (rounds_since_mod < 300) return 0;

    /* 3. fear < 0.3 */
    if (fear >= 0.3f) return 0;

    /* 4. desire >= 0.3 */
    if (desire < 0.3f) return 0;

    /* 5. No blackboard entries in last 200 ticks */
    if (current_tick - last_bb_tick < 200) return 0;

    return 1;
}

/* ── Phase 1: Replay ───────────────────────────────────────────── */
void idler_replay(void) {
    if (g_event_count == 0) {
        printf("  IDLE: replay: no events to replay\n");
        return;
    }

    printf("  IDLE: replaying %d events from tick %u to tick %u\n",
           g_event_count, g_replay_start_tick, g_replay_end_tick);

    /* Find temporal patterns: consecutive event type pairs */
    int pair_count[7][7]; /* type 1-6 × type 1-6 */
    memset(pair_count, 0, sizeof(pair_count));

    /* Find: failure often followed by what? */
    int fail_followed_by[7];
    memset(fail_followed_by, 0, sizeof(fail_followed_by));

    /* Find: what precedes success? */
    int precedes_success[7];
    memset(precedes_success, 0, sizeof(precedes_success));

    for (int i = 1; i < g_event_count; i++) {
        uint8_t prev_type = g_events[i-1].type;
        uint8_t curr_type = g_events[i].type;

        if (prev_type >= 1 && prev_type <= 6 &&
            curr_type >= 1 && curr_type <= 6) {
            pair_count[prev_type][curr_type]++;

            if (prev_type == BB_TYPE_OPT_FAIL)
                fail_followed_by[curr_type]++;

            if (curr_type == BB_TYPE_OPT_SUCCESS)
                precedes_success[prev_type]++;
        }
    }

    /* Report top patterns */
    int patterns_found = 0;
    for (int p = 1; p <= 6 && patterns_found < 3; p++) {
        if (fail_followed_by[p] >= 3) {
            const char *type_name = "unknown";
            switch (p) {
                case 1: type_name = "optimization_success"; break;
                case 2: type_name = "optimization_failure"; break;
                case 3: type_name = "temperature_event"; break;
                case 4: type_name = "instinct_event"; break;
                case 5: type_name = "fission_event"; break;
                case 6: type_name = "param_adjust"; break;
            }
            printf("  IDLE: pattern: optimization_failure often followed by %s (%d times)\n",
                   type_name, fail_followed_by[p]);
            patterns_found++;
        }
    }

    for (int p = 1; p <= 6 && patterns_found < 5; p++) {
        if (precedes_success[p] >= 3 && p != BB_TYPE_OPT_SUCCESS) {
            const char *type_name = "unknown";
            switch (p) {
                case 1: type_name = "optimization_success"; break;
                case 2: type_name = "optimization_failure"; break;
                case 3: type_name = "temperature_event"; break;
                case 4: type_name = "instinct_event"; break;
                case 5: type_name = "fission_event"; break;
                case 6: type_name = "param_adjust"; break;
            }
            printf("  IDLE: pattern: %s often precedes optimization_success (%d times)\n",
                   type_name, precedes_success[p]);
            patterns_found++;
        }
    }

    printf("  IDLE: replay complete, %d patterns found\n", patterns_found);
}

/* ── Phase 2: Associate ─────────────────────────────────────────── */
void idler_associate(void) {
    memset(g_matrix, 0, sizeof(g_matrix));

    printf("  IDLE: associating cross-domain events...\n");

    /* Build co-occurrence matrix from events */
    for (int i = 0; i < g_event_count; i++) {
        struct bb_event *e = &g_events[i];

        /* Determine row (outcome) */
        int row = -1;
        if (e->type == BB_TYPE_OPT_SUCCESS) row = 0;
        else if (e->type == BB_TYPE_OPT_FAIL) row = 1;
        else if (e->type == BB_TYPE_FISSION) row = 2;

        if (row < 0) continue;

        /* Determine column (condition) from surrounding context */
        /* temp_high: if another temp event with value>=2 nearby */
        /* temp_low: temp event with value<2 nearby */
        /* desire_high: instinct event with value>=2 nearby */
        /* desire_low: instinct event with value<2 nearby */
        /* fear_high: instinct event with payload indicating fear */

        int window = 20; /* look within ±20 ticks */
        for (int j = 0; j < g_event_count; j++) {
            if (i == j) continue;
            int32_t dt = (int32_t)(g_events[j].tick - e->tick);
            if (dt < -window || dt > window) continue;

            if (g_events[j].type == BB_TYPE_TEMP_EVENT) {
                if (g_events[j].value >= 2) g_matrix[row][0]++; /* temp_high */
                else g_matrix[row][1]++; /* temp_low */
            }
            if (g_events[j].type == BB_TYPE_INSTINCT) {
                if (g_events[j].value >= 2) g_matrix[row][2]++; /* desire_high */
                else g_matrix[row][3]++; /* desire_low */
                if (g_events[j].payload & 1) g_matrix[row][4]++; /* fear_high */
            }
        }
    }

    /* Print co-occurrence matrix */
    int any_data = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (g_matrix[r][c] > 0) any_data = 1;

    if (any_data) {
        printf("  IDLE: co-occurrence matrix:\n");
        printf("         ");
        for (int c = 0; c < COLS; c++) printf("%8s", col_labels[c]);
        printf("\n");
        for (int r = 0; r < ROWS; r++) {
            printf("  %-9s", row_labels[r]);
            for (int c = 0; c < COLS; c++)
                printf("%8d", g_matrix[r][c]);
            printf("\n");
        }
    } else {
        printf("  IDLE: no cross-domain associations found\n");
    }
}

/* ── Phase 3: Discover ──────────────────────────────────────────── */
int idler_discover(void) {
    int discoveries = 0;

    /* Extract candidate rules from matrix patterns */
    /* Rule 1: opt_ok is high when temp_low → optimize more aggressively at low temp */
    if (g_matrix[0][1] > g_matrix[0][0] && g_matrix[0][1] >= 5) {
        struct tork_rule r;
        memset(&r, 0, sizeof(r));
        r.type = RULE_TYPE_CODE;
        r.active = 0;
        r.confidence = (uint8_t)(g_matrix[0][1] * 100 /
                         (g_matrix[0][0] + g_matrix[0][1] + 1));
        if (r.confidence > 100) r.confidence = 100;
        strncpy(r.premise, "temp_low and desire_high", 32);
        r.premise_len = (uint8_t)strlen(r.premise);
        strncpy(r.conclusion, "try aggressive optimization", 32);
        r.conclusion_len = (uint8_t)strlen(r.conclusion);
        r._reserved[0] = 1; /* source = idle_discovery */

        int slot = ind_save_rule(&r);
        if (slot >= 0) {
            printf("  IDLE: discovered: '%.32s' → '%.32s' conf=%d%% (slot %d)\n",
                   r.premise, r.conclusion, r.confidence, slot);
            discoveries++;
        }
    }

    /* Rule 2: fission_win rare when fear_high → avoid fission in fear zone */
    if (g_matrix[2][4] > 0 && g_matrix[2][4] < g_matrix[2][2]) {
        struct tork_rule r;
        memset(&r, 0, sizeof(r));
        r.type = RULE_TYPE_INSTINCT;
        r.active = 0;
        r.confidence = (uint8_t)(g_matrix[2][2] * 100 /
                         (g_matrix[2][4] + g_matrix[2][2] + 1));
        if (r.confidence > 100) r.confidence = 100;
        strncpy(r.premise, "fear_high and desire_low", 32);
        r.premise_len = (uint8_t)strlen(r.premise);
        strncpy(r.conclusion, "suppress fission trigger", 32);
        r.conclusion_len = (uint8_t)strlen(r.conclusion);
        r._reserved[0] = 1;

        int slot = ind_save_rule(&r);
        if (slot >= 0) {
            printf("  IDLE: discovered: '%.32s' → '%.32s' conf=%d%% (slot %d)\n",
                   r.premise, r.conclusion, r.confidence, slot);
            discoveries++;
        }
    }

    /* Rule 3: opt_fail with desire_high → retry conservative instead */
    if (g_matrix[1][2] > g_matrix[1][3] && g_matrix[1][2] >= 3) {
        struct tork_rule r;
        memset(&r, 0, sizeof(r));
        r.type = RULE_TYPE_CODE;
        r.active = 0;
        r.confidence = (uint8_t)(g_matrix[1][2] * 100 /
                         (g_matrix[1][2] + g_matrix[1][3] + 1));
        if (r.confidence > 100) r.confidence = 100;
        strncpy(r.premise, "desire_high and opt_recent_fail", 32);
        r.premise_len = (uint8_t)strlen(r.premise);
        strncpy(r.conclusion, "use conservative optimization", 32);
        r.conclusion_len = (uint8_t)strlen(r.conclusion);
        r._reserved[0] = 1;

        int slot = ind_save_rule(&r);
        if (slot >= 0) {
            printf("  IDLE: discovered: '%.32s' → '%.32s' conf=%d%% (slot %d)\n",
                   r.premise, r.conclusion, r.confidence, slot);
            discoveries++;
        }
    }

    if (discoveries == 0)
        printf("  IDLE: no candidate rules discovered\n");
    else
        printf("  IDLE: %d candidate rules discovered\n", discoveries);

    return discoveries;
}

/* ── Idle cycle ─────────────────────────────────────────────────── */
int idler_cycle(void) {
    g_last_discoveries = 0;

    /* Set replay window from current tick context */
    /* We use the latest event tick as end; if no events, skip */
    g_replay_end_tick = 0;
    g_replay_start_tick = 0;

    /* Quick scan to find latest tick */
    uint32_t latest = 0;

    /* Temporarily collect ALL events to find latest tick */
    g_event_count = 0;
    g_replay_start_tick = 0;
    g_replay_end_tick = 0xFFFFFFFF; /* collect all */
    bb_read_all(collect_events_cb);

    for (int i = 0; i < g_event_count; i++) {
        if (g_events[i].tick > latest)
            latest = g_events[i].tick;
    }

    if (latest == 0) {
        printf("  IDLE: no blackboard events, skipping idle\n");
        return 0;
    }

    g_replay_end_tick = latest;

    /* Phase 1: Replay */
    idler_replay();

    /* Phase 2: Associate */
    idler_associate();

    /* Phase 3: Discover */
    g_last_discoveries = idler_discover();

    return g_last_discoveries;
}

/* ── Accessors ──────────────────────────────────────────────────── */
int idler_active(void) { return g_active; }
void idler_set_active(int active) { g_active = active; }
int idler_last_discoveries(void) { return g_last_discoveries; }
