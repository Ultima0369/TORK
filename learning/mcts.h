#ifndef MCTS_H
#define MCTS_H

#include <stdint.h>

/* ── MCTS (Monte Carlo Tree Search) Decision Engine ────────
 *  Lightweight implementation for TORK's idle learning loop.
 *  Uses experience buffer for simulation outcomes.
 *  No external dependencies — pure C, single-threaded.
 * ──────────────────────────────────────────────────────────── */

/* Maximum tree size (nodes visited per search) */
#define MCTS_MAX_NODES   512

/* Number of simulations per node */
#define MCTS_SIMULATIONS 128

/* Exploration constant (higher = more exploration) */
#define MCTS_EXPLORATION 1.414f

/* ── Action space ───────────────────────────────────────────
 *  These are the decisions TORK can make in each state.
 *  Each has a type and a continuous parameter (-128..127).
 * ──────────────────────────────────────────────────────────── */

typedef enum {
    MCTS_ADJUST_FEAR        = 0,  /* Raise/lower fear sensitivity */
    MCTS_ADJUST_CURIOSITY   = 1,  /* Raise/lower curiosity drive */
    MCTS_ADJUST_HEARTBEAT   = 2,  /* Speed up/slow down core loop */
    MCTS_TRY_MODIFY         = 3,  /* Attempt code modification */
    MCTS_TRY_OPTIMIZE       = 4,  /* Attempt code optimization */
    MCTS_ENTER_IDLE         = 5,  /* Enter idle learning mode */
    MCTS_CALL_CLOUD         = 6,  /* Ask cloud for guidance */
    MCTS_NUM_ACTIONS        = 7
} mcts_action_type_t;

/* ── A single action (what to do) ─────────────────────────── */
typedef struct {
    uint8_t type;            /* mcts_action_type_t */
    int8_t  param;           /* -128..127 */
} mcts_action_t;

/* ── State representation (input to MCTS) ──────────────────── */
typedef struct {
    uint8_t  hw_stress;      /* Current CPU stress */
    int8_t   drive;          /* Current drive value */
    uint16_t gen_count;      /* Current generation */
    uint8_t  recent_crashes; /* Crashes in last 100 ticks */
    float    exp_success[MCTS_NUM_ACTIONS]; /* Historical success rates */
} mcts_state_t;

/* ── MCTS result ──────────────────────────────────────────── */
typedef struct {
    mcts_action_t action;           /* Best action found */
    float          expected_value;  /* Expected outcome (-1..1) */
    int            simulations_run; /* How many simulations were run */
    int            nodes_visited;   /* How many nodes were expanded */
    float          confidence;      /* 0..1: how sure the search is */
} mcts_result_t;

/* ── Public API ───────────────────────────────────────────── */

/* Run MCTS from a given state. Returns the best action found. */
mcts_result_t mcts_search(const mcts_state_t *state, int time_budget_ms);

/* Evaluate an action from a given state (used by simulation) */
float mcts_evaluate(const mcts_state_t *state, const mcts_action_t *action);

/* Print a human-readable summary of the search result */
void mcts_print_result(const mcts_result_t *result);

/* Get the name of an action type */
const char *mcts_action_name(uint8_t type);

#endif /* MCTS_H */

/* ── Auto-tuning API ──────────────────────────────────────── */

/* Tune MCTS parameters based on recent experience outcomes */
void mcts_auto_tune(void);

/* Get current exploration constant */
float mcts_get_exploration(void);

/* Get current min iterations */
int mcts_get_min_iterations(void);

/* Get tuning count */
int mcts_tuning_count(void);
