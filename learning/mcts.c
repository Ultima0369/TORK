#include "mcts.h"
#include "experience.h"
#include "pi_seed.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ── Tree node (internal) ──────────────────────────────────── */
typedef struct mcts_node {
    mcts_action_t  action;              /* Action that led to this node */
    mcts_state_t   state;               /* State at this node */
    
    int            visit_count;         /* How many times visited */
    float          total_value;         /* Sum of simulation values */
    
    struct mcts_node *parent;           /* Parent node (NULL for root) */
    struct mcts_node *children[MCTS_NUM_ACTIONS]; /* Child nodes */
    int              num_children;      /* How many children expanded */
    
    int              is_terminal;       /* 1 if simulation should stop */
} mcts_node_t;

static mcts_node_t g_nodes[MCTS_MAX_NODES];
static int g_node_count = 0;

/* ── Node pool management ──────────────────────────────────── */
static mcts_node_t *alloc_node(void) {
    if (g_node_count >= MCTS_MAX_NODES) return NULL;
    mcts_node_t *n = &g_nodes[g_node_count++];
    memset(n, 0, sizeof(*n));
    return n;
}

static void reset_pool(void) {
    g_node_count = 0;
}

/* ── UCB1 calculation (Upper Confidence Bound) ─────────────── */
static float ucb1(const mcts_node_t *node, int parent_visits) {
    if (node->visit_count == 0) return 1e9f; /* Always explore unvisited */
    
    float exploitation = node->total_value / node->visit_count;
    float exploration = mcts_get_exploration() * sqrtf(logf((float)parent_visits) / node->visit_count);
    
    return exploitation + exploration;
}

/* ── Select: traverse tree using UCB1 ──────────────────────── */
static mcts_node_t *select_node(mcts_node_t *node) {
    while (node->num_children > 0) {
        /* Pick child with highest UCB1 */
        mcts_node_t *best = NULL;
        float best_ucb = -1e9f;
        
        for (int i = 0; i < node->num_children; i++) {
            float ucb = ucb1(node->children[i], node->visit_count);
            if (ucb > best_ucb) {
                best_ucb = ucb;
                best = node->children[i];
            }
        }
        
        if (!best) break;
        node = best;
    }
    return node;
}

/* ── Expand: add child nodes ────────────────────────────────── */
static int expand_node(mcts_node_t *node) {
    if (node->is_terminal) return 0;
    
    int expanded = 0;
    for (int t = 0; t < MCTS_NUM_ACTIONS; t++) {
        if (node->num_children >= MCTS_NUM_ACTIONS) break;
        
        mcts_node_t *child = alloc_node();
        if (!child) break;
        
        child->action.type = (uint8_t)t;
        child->action.param = 0;
        child->state = node->state;
        child->parent = node;
        
        /* Apply state transition */
        switch (t) {
            case MCTS_ADJUST_FEAR:
                child->state.drive = node->state.drive + 10;
                break;
            case MCTS_ADJUST_CURIOSITY:
                child->state.drive = node->state.drive + 15;
                break;
            case MCTS_ADJUST_HEARTBEAT:
                child->state.hw_stress = (node->state.hw_stress > 0) ? 
                                         node->state.hw_stress - 1 : 0;
                break;
            case MCTS_TRY_MODIFY:
                /* Modification usually increases stress temporarily */
                child->state.hw_stress = (node->state.hw_stress < 3) ?
                                         node->state.hw_stress + 1 : 3;
                break;
            case MCTS_TRY_OPTIMIZE:
                child->state.hw_stress = (node->state.hw_stress < 3) ?
                                         node->state.hw_stress + 1 : 3;
                break;
            case MCTS_ENTER_IDLE:
                child->state.hw_stress = (node->state.hw_stress > 0) ?
                                         node->state.hw_stress - 1 : 0;
                break;
            case MCTS_CALL_CLOUD:
                /* Cloud call doesn't change local state immediately */
                break;
            case MCTS_MOD_REPLACE_OP:
                /* 替换操作数：临时增加压力（风险操作） */
                child->state.hw_stress = (node->state.hw_stress < 3) ?
                                         node->state.hw_stress + 1 : 3;
                break;
            case MCTS_MOD_DEL_DEAD:
                /* 删除死代码：降低压力（清理操作） */
                child->state.hw_stress = (node->state.hw_stress > 0) ?
                                         node->state.hw_stress - 1 : 0;
                break;
            case MCTS_MOD_DEL_NOP:
                /* 删除 NOP：中性操作 */
                break;
            case MCTS_MOD_SWAP_REGS:
                /* 寄存器交换：临时增加压力 */
                child->state.hw_stress = (node->state.hw_stress < 3) ?
                                         node->state.hw_stress + 1 : 3;
                break;
        }
        
        node->children[node->num_children++] = child;
        expanded++;
    }
    
    return expanded;
}

/* ── Simulate: random rollout from node ─────────────────────── */
static float simulate_node(mcts_node_t *node, int depth) {
    if (depth > 20 || node->is_terminal) return 0.0f;
    
    /* Look up historical experience to inform simulation */
    experience_t recent[5];
    int n = exp_recent(5, recent);
    (void)n;
    
    /* Base value from state quality */
    float value = 0.0f;
    
    /* Lower stress = better */
    value += (3.0f - node->state.hw_stress) * 0.1f;
    
    /* Positive drive = good */
    value += node->state.drive * 0.003f;
    
    /* Higher gen count = more evolved = better */
    value += node->state.gen_count * 0.001f;

    /* 代码修改子动作：根据压力和历史成功率给分 */
    switch (node->action.type) {
    case MCTS_MOD_REPLACE_OP:
        /* 低压力时替换操作数风险低，价值高 */
        if (node->state.hw_stress == 0) value += 0.25f;
        else value -= 0.1f;
        break;
    case MCTS_MOD_DEL_DEAD:
        /* 删除死代码总是有益的 */
        value += 0.2f;
        break;
    case MCTS_MOD_DEL_NOP:
        /* 删除 NOP 收益较小但安全 */
        value += 0.15f;
        break;
    case MCTS_MOD_SWAP_REGS:
        /* 寄存器优化需要低压力 */
        if (node->state.hw_stress <= 1) value += 0.2f;
        else value -= 0.05f;
        break;
    default:
        break;
    }

    /* Check historical success rates */
    if (node->action.type < MCTS_NUM_ACTIONS) {
        float sr = node->state.exp_success[node->action.type];
        if (sr >= 0) {
            value += sr * 0.3f;
        }
    }
    
    /* Small randomness for exploration */
    value += pi_seed_float() * 0.1f;
    
    return value;
}

/* ── Backpropagate: update statistics up the tree ──────────── */
static void backpropagate(mcts_node_t *node, float value) {
    while (node) {
        node->visit_count++;
        node->total_value += value;
        node = node->parent;
    }
}

/* ── Run one iteration of MCTS ──────────────────────────────── */
static void run_iteration(mcts_node_t *root) {
    /* 1. SELECT */
    mcts_node_t *selected = select_node(root);
    
    /* 2. EXPAND */
    if (selected->visit_count > 0) {
        expand_node(selected);
        /* Pick first child for simulation if available */
        if (selected->num_children > 0) {
            selected = selected->children[0];
        }
    }
    
    /* 3. SIMULATE */
    float value = simulate_node(selected, 0);
    
    /* 4. BACKPROPAGATE */
    backpropagate(selected, value);
}

/* ── Public: MCTS search entry point ───────────────────────── */
mcts_result_t mcts_search(const mcts_state_t *state, int time_budget_ms) {
    mcts_result_t result;
    memset(&result, 0, sizeof(result));
    
    if (!state) return result;
    
    reset_pool();
    
    /* Create root node */
    mcts_node_t *root = alloc_node();
    if (!root) return result;
    
    root->state = *state;
    root->action.type = 255; /* Root has no action */
    root->action.param = 0;
    
    /* Set experience success rates from historical data */
    for (int t = 0; t < MCTS_NUM_ACTIONS; t++) {
        /* Override state's exp_success with real data */
        ((mcts_state_t*)state)->exp_success[t] = exp_success_rate((uint8_t)t);
    }
    root->state = *state; /* Re-read */
    
    /* Calculate iterations based on time budget */
    int iterations = (time_budget_ms > 0) ? (time_budget_ms * 100) : mcts_get_min_iterations();
    if (iterations > 50000) iterations = 50000;
    
    /* Run MCTS iterations */
    int sim_count = 0;
    for (int i = 0; i < iterations && g_node_count < MCTS_MAX_NODES; i++) {
        run_iteration(root);
        sim_count++;
    }
    
    /* Find best child */
    mcts_node_t *best = NULL;
    float best_value = -1e9f;
    
    for (int i = 0; i < root->num_children; i++) {
        mcts_node_t *child = root->children[i];
        if (child->visit_count == 0) continue;
        float avg = child->total_value / child->visit_count;
        
        if (avg > best_value) {
            best_value = avg;
            best = child;
        }
    }
    
    if (best) {
        result.action = best->action;
        result.expected_value = best_value;
        result.simulations_run = sim_count;
        result.nodes_visited = g_node_count;
        result.confidence = (best->visit_count > 10) ? 
                            (best->visit_count / (float)(root->visit_count)) : 0.1f;
    } else {
        /* Fallback: choose first action */
        result.action.type = MCTS_ADJUST_CURIOSITY;
        result.action.param = 10;
        result.expected_value = 0.0f;
        result.confidence = 0.0f;
    }
    
    return result;
}

/* ── Evaluate a single action (for direct use, not MCTS internal) ── */
float mcts_evaluate(const mcts_state_t *state, const mcts_action_t *action) {
    if (!state || !action) return 0.0f;
    
    float value = 0.0f;
    
    /* Historical experience */
    float sr = exp_success_rate(action->type);
    if (sr >= 0) value += sr * 0.5f;
    
    /* State-based evaluation */
    switch (action->type) {
        case MCTS_ADJUST_FEAR:
            /* If stress is high, adjusting fear is valuable */
            if (state->hw_stress >= 2) value += 0.3f;
            break;
        case MCTS_ADJUST_CURIOSITY:
            /* If drive is low, boosting curiosity helps */
            if (state->drive < 0) value += 0.3f;
            break;
        case MCTS_ADJUST_HEARTBEAT:
            /* If stress is high, slowing down helps */
            if (state->hw_stress >= 2) value += 0.4f;
            break;
        case MCTS_TRY_MODIFY:
            /* Safe to modify when stress is low */
            if (state->hw_stress == 0) value += 0.3f;
            break;
        case MCTS_TRY_OPTIMIZE:
            if (state->hw_stress <= 1) value += 0.2f;
            break;
        case MCTS_ENTER_IDLE:
            /* Idle when drive is neutral and no stress */
            if (state->hw_stress == 0 && state->drive >= -10 && state->drive <= 10)
                value += 0.3f;
            break;
        case MCTS_CALL_CLOUD:
            /* Cloud is always an option, but better when stuck */
            if (state->drive < -20 || state->recent_crashes > 3)
                value += 0.5f;
            break;
        case MCTS_MOD_REPLACE_OP:
            if (state->hw_stress == 0) value += 0.35f;
            break;
        case MCTS_MOD_DEL_DEAD:
            value += 0.25f;  /* dead code deletion is almost always safe */
            break;
        case MCTS_MOD_DEL_NOP:
            value += 0.15f;
            break;
        case MCTS_MOD_SWAP_REGS:
            if (state->hw_stress <= 1) value += 0.25f;
            break;
    }
    
    return value;
}

/* ── Print result ──────────────────────────────────────────── */
void mcts_print_result(const mcts_result_t *result) {
    if (!result) return;
    printf("  MCTS: action=%s(param=%d) value=%.3f conf=%.2f sims=%d nodes=%d\n",
           mcts_action_name(result->action.type),
           result->action.param,
           result->expected_value,
           result->confidence,
           result->simulations_run,
           result->nodes_visited);
}

/* ── Action name ───────────────────────────────────────────── */
const char *mcts_action_name(uint8_t type) {
    static const char *names[MCTS_NUM_ACTIONS] = {
        "fear_adjust", "curiosity", "heartbeat",
        "modify", "optimize", "idle", "call_cloud",
        "replace_op", "del_dead", "del_nop", "swap_regs"
    };
    if (type < MCTS_NUM_ACTIONS) return names[type];
    return "unknown";
}

/* ── Auto-tuning ─────────────────────────────────────────────
 *  Adjusts MCTS parameters based on historical outcome feedback.
 *  Called periodically by the engine idle loop.
 * ──────────────────────────────────────────────────────────── */

/* Tunable parameters */
static struct {
    float exploration;          /* UCB1 exploration constant */
    int   simulations_per_node; /* Simulations per node */
    int   min_iterations;       /* Minimum iterations per search */
    int   tuning_count;         /* How many times tuned */
} mcts_tune = {
    .exploration        = MCTS_EXPLORATION,
    .simulations_per_node = 128,
    .min_iterations     = 100,
    .tuning_count       = 0,
};

/* Tune parameters based on recent experience outcomes */
void mcts_auto_tune(void) {
    mcts_tune.tuning_count++;
    
    /* Get recent 20 experiences */
    experience_t recent[20];
    int n = exp_recent(20, recent);
    if (n < 5) return;  /* Not enough data */
    
    /* Calculate average outcome */
    int total_outcome = 0;
    int crashes = 0;
    for (int i = 0; i < n; i++) {
        total_outcome += recent[i].outcome;
        if (recent[i].crash_occurred) crashes++;
    }
    float avg_outcome = (float)total_outcome / n;
    float crash_rate = (float)crashes / n;
    
    printf("  TUNE: avg_outcome=%.1f crash_rate=%.2f (tuning #%d)\n",
           avg_outcome, crash_rate, mcts_tune.tuning_count);
    
    /* Adjust exploration: if outcomes are bad, explore more */
    if (avg_outcome < -10.0f && mcts_tune.exploration < 5.0f) {
        mcts_tune.exploration += 0.1f;
        printf("  TUNE: ↑ exploration → %.2f\n", mcts_tune.exploration);
    } else if (avg_outcome > 15.0f && mcts_tune.exploration > 0.5f) {
        mcts_tune.exploration -= 0.05f;
        printf("  TUNE: ↓ exploration → %.2f (exploiting known good paths)\n", mcts_tune.exploration);
    }
    
    /* Adjust iterations: if crash rate is high, do more iterations (be more careful) */
    if (crash_rate > 0.2f) {
        mcts_tune.min_iterations += 50;
        if (mcts_tune.min_iterations > 2000) mcts_tune.min_iterations = 2000;
        printf("  TUNE: ↑ min_iterations → %d (crash_rate=%.2f)\n", 
               mcts_tune.min_iterations, crash_rate);
    } else if (crash_rate < 0.05f && mcts_tune.min_iterations > 100) {
        mcts_tune.min_iterations -= 50;
        printf("  TUNE: ↓ min_iterations → %d (environment stable)\n", mcts_tune.min_iterations);
    }
}

/* Get current exploration constant (used by UCB1) */
float mcts_get_exploration(void) {
    return mcts_tune.exploration;
}

/* Get current min iterations */
int mcts_get_min_iterations(void) {
    return mcts_tune.min_iterations;
}

/* Get tuning count */
int mcts_tuning_count(void) {
    return mcts_tune.tuning_count;
}
