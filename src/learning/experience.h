#ifndef EXPERIENCE_H
#define EXPERIENCE_H

#include <stdint.h>
#include <time.h>

/* ── Experience Ring Buffer ──────────────────────────────────────
 *  Stores TORK's recent (state, action, outcome) tuples.
 *  Used by MCTS for simulation and by idler for pattern discovery.
 *  Persisted to disk so learning survives restarts.
 * ──────────────────────────────────────────────────────────────── */

/* Maximum experiences stored in ring buffer */
#define EXP_MAX_EXPERIENCES  4096
#define EXP_PATH             "persist/experience.bin"

/* ── Experience entry ─────────────────────────────────────── */
typedef struct {
    uint64_t tick;               /* When this experience occurred */
    int64_t  timestamp_ns;       /* Nanosecond-precision timestamp */
    
    /* Input state (soul snapshot at decision time) */
    uint8_t  hw_stress;          /* CPU stress level 0-3 */
    int8_t   drive_pre;          /* Drive value BEFORE action */
    uint16_t gen_count;          /* Evolution generation */
    
    /* Action taken */
    uint8_t  action_type;        /* 0=adjust_fear, 1=adjust_curiosity, 
                                    2=adjust_heartbeat, 3=try_modify, 
                                    4=try_optimize, 5=enter_idle, 6=call_cloud */
    int8_t   action_param;       /* Parameter for the action (-128..127) */
    
    /* Outcome (filled AFTER action result is known) */
    int8_t   outcome;            /* -100..100: how good was the result */
    uint8_t  crash_occurred;     /* 1 if action caused crash */
    uint8_t  compile_ok;         /* 1 if code modification compiled */
    
    /* Post-action state */
    uint8_t  hw_stress_post;     /* HW stress after action */
    int8_t   drive_post;         /* Drive value after action */
    
    uint8_t  _pad[6];            /* Future use */
} __attribute__((packed)) experience_t;

/* Compile-time assertion: must be exact size for binary persistence */
_Static_assert(sizeof(experience_t) == 33, "experience_t must be 33 bytes");

/* ── Ring buffer ────────────────────────────────────────────── */
typedef struct {
    uint32_t  head;              /* Write position (mod EXP_MAX_EXPERIENCES) */
    uint32_t  count;             /* Total experiences written */
    int16_t   elite_threshold;   /* 精英保留: top 10% outcome 阈值 */
    experience_t slots[EXP_MAX_EXPERIENCES];
} experience_buffer_t;

_Static_assert(sizeof(experience_buffer_t) == 12 + EXP_MAX_EXPERIENCES * 33,
               "experience_buffer_t size check");

/* ── Public API ─────────────────────────────────────────────── */

/* Initialize ring buffer. If persist file exists, load it. */
void exp_init(void);

/* Save ring buffer to persist file */
void exp_save(void);

/* Record a new experience. Thread-safe (single-writer assumed). */
void exp_record(uint64_t tick, uint8_t hw_stress, int8_t drive_pre,
                uint16_t gen_count, uint8_t action_type, int8_t action_param,
                int8_t outcome, uint8_t crash, uint8_t compile_ok,
                uint8_t hw_stress_post, int8_t drive_post);

/* Get the most recent N experiences. Returns actual count (may be < N). */
int exp_recent(int n, experience_t *out);
int exp_read(int idx, experience_t *out);      /* Read by index (mod buffer) */

/* Get experiences matching a filter. Returns count. */
int exp_filter(uint8_t action_type, int max_results, experience_t *out);

/* Get the total number of experiences recorded */
uint32_t exp_count(void);

/* Get success rate for a given action type (0.0..1.0). -1 if no data. */
float exp_success_rate(uint8_t action_type);

/* ── Experience update API ─────────────────────────────────── */

/* Update the outcome of the most recent experience (written by engine after action) */
void exp_update_last(int8_t outcome, uint8_t crash, uint8_t compile_ok,
                     uint8_t hw_stress_post, int8_t drive_post);

/* Get a pointer to the most recent experience (read-only) */
const experience_t *exp_last(void);

#endif /* EXPERIENCE_H */
