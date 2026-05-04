#include "mutation_guide.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static mutation_guide_t g_mg;
static int g_initialized = 0;

void mg_init(void) {
    memset(&g_mg, 0, sizeof(g_mg));
    
    /* Register known strategies with initial weights */
    mg_strategy_t *s;
    
    s = &g_mg.strategies[g_mg.strategy_count++];
    s->strategy = MG_STRATEGY_RANDOM;
    s->weight = 0.1f; s->attempts = 0; s->successes = 0; s->success_rate = 0.0f;
    snprintf(s->name, sizeof(s->name), "纯随机");
    
    s = &g_mg.strategies[g_mg.strategy_count++];
    s->strategy = MG_STRATEGY_INSTINCT;
    s->weight = 0.25f; s->attempts = 0; s->successes = 0; s->success_rate = 0.0f;
    snprintf(s->name, sizeof(s->name), "本能参数");
    
    s = &g_mg.strategies[g_mg.strategy_count++];
    s->strategy = MG_STRATEGY_PATTERN_THRESHOLD;
    s->weight = 0.15f; s->attempts = 0; s->successes = 0; s->success_rate = 0.0f;
    snprintf(s->name, sizeof(s->name), "模式阈值");
    
    s = &g_mg.strategies[g_mg.strategy_count++];
    s->strategy = MG_STRATEGY_LEARNING_RATE;
    s->weight = 0.15f; s->attempts = 0; s->successes = 0; s->success_rate = 0.0f;
    snprintf(s->name, sizeof(s->name), "学习率");
    
    s = &g_mg.strategies[g_mg.strategy_count++];
    s->strategy = MG_STRATEGY_CURIOSITY;
    s->weight = 0.15f; s->attempts = 0; s->successes = 0; s->success_rate = 0.0f;
    snprintf(s->name, sizeof(s->name), "好奇心衰减");
    
    s = &g_mg.strategies[g_mg.strategy_count++];
    s->strategy = MG_STRATEGY_BRANCH_LIFETIME;
    s->weight = 0.1f; s->attempts = 0; s->successes = 0; s->success_rate = 0.0f;
    snprintf(s->name, sizeof(s->name), "分支寿命");
    
    s = &g_mg.strategies[g_mg.strategy_count++];
    s->strategy = MG_STRATEGY_ENERGY_PARAMS;
    s->weight = 0.05f; s->attempts = 0; s->successes = 0; s->success_rate = 0.0f;
    snprintf(s->name, sizeof(s->name), "能量参数");
    
    s = &g_mg.strategies[g_mg.strategy_count++];
    s->strategy = MG_STRATEGY_WATCHER_FOCUS;
    s->weight = 0.05f; s->attempts = 0; s->successes = 0; s->success_rate = 0.0f;
    snprintf(s->name, sizeof(s->name), "观察焦点");
    
    printf("  MGUIDE: %d strategies registered\n", g_mg.strategy_count);
    g_initialized = 1;
}

mg_strategy_type_t mg_recommend(char *recommendation, int buf_size) {
    if (!g_initialized || g_mg.total_attempts == 0) {
        /* No data yet: use instinct (default best guess) */
        if (recommendation) snprintf(recommendation, buf_size, 
            "初试: 修改本能参数(好奇心)");
        return MG_STRATEGY_INSTINCT;
    }
    
    /* Weighted random selection */
    float total_weight = 0;
    for (uint32_t i = 0; i < g_mg.strategy_count; i++) {
        mg_strategy_t *s = &g_mg.strategies[i];
        /* Update success rate */
        if (s->attempts > 0) {
            s->success_rate = (float)s->successes / (float)s->attempts;
            /* Weight = base_weight + bonus for high success rate */
            s->weight = 0.05f + s->success_rate * 0.8f;
            /* Exploration bonus: try less-tested strategies occasionally */
            if (s->attempts < 3) s->weight += 0.2f;
        }
        total_weight += s->weight;
    }
    
    /* Pick based on weights */
    float roll = (float)rand() / (float)RAND_MAX * total_weight;
    float cumulative = 0;
    for (uint32_t i = 0; i < g_mg.strategy_count; i++) {
        cumulative += g_mg.strategies[i].weight;
        if (roll <= cumulative) {
            mg_strategy_t *s = &g_mg.strategies[i];
            if (recommendation) {
                snprintf(recommendation, buf_size, 
                    "%s (成功率%.0f%%, 尝试%d次)", 
                    s->name, s->success_rate * 100, s->attempts);
            }
            return s->strategy;
        }
    }
    
    if (recommendation) snprintf(recommendation, buf_size, "纯随机(兜底)");
    return MG_STRATEGY_RANDOM;
}

void mg_record_result(mg_strategy_type_t strategy, int success,
                      float fitness_before, float fitness_after,
                      const char *description) {
    if (!g_initialized) return;
    
    /* Update strategy stats */
    for (uint32_t i = 0; i < g_mg.strategy_count; i++) {
        if (g_mg.strategies[i].strategy == strategy) {
            g_mg.strategies[i].attempts++;
            if (success) g_mg.strategies[i].successes++;
            g_mg.strategies[i].success_rate = 
                (float)g_mg.strategies[i].successes / 
                (float)g_mg.strategies[i].attempts;
            break;
        }
    }
    
    g_mg.total_attempts++;
    if (success) g_mg.total_successes++;
    
    /* Record to history */
    mg_record_t *r = &g_mg.history[g_mg.history_head];
    r->id = g_mg.history_head;
    r->strategy = strategy;
    r->gen = g_mg.total_attempts;
    r->success = success;
    r->fitness_before = fitness_before;
    r->fitness_after = fitness_after;
    r->tick = g_mg.history_head;
    snprintf(r->description, sizeof(r->description), "%s", 
             description ? description : "");
    
    g_mg.history_head = (g_mg.history_head + 1) % MG_HISTORY_SIZE;
}

mg_strategy_type_t mg_best_strategy(void) {
    if (!g_initialized) return MG_STRATEGY_RANDOM;
    
    float best_rate = -1;
    mg_strategy_type_t best = MG_STRATEGY_RANDOM;
    
    for (uint32_t i = 0; i < g_mg.strategy_count; i++) {
        if (g_mg.strategies[i].attempts >= 3 && 
            g_mg.strategies[i].success_rate > best_rate) {
            best_rate = g_mg.strategies[i].success_rate;
            best = g_mg.strategies[i].strategy;
        }
    }
    
    return best;
}

void mg_summary(char *buf, int buf_size) {
    if (!g_initialized || !buf || buf_size < 1) return;
    
    char tmp[512];
    int pos = 0;
    pos += snprintf(tmp + pos, sizeof(tmp) - pos,
        "MGUIDE: total=%d success=%d rate=%.0f%%\n",
        g_mg.total_attempts, g_mg.total_successes,
        g_mg.total_attempts > 0 ? 
            (float)g_mg.total_successes / g_mg.total_attempts * 100 : 0);
    
    for (uint32_t i = 0; i < g_mg.strategy_count && pos < (int)sizeof(tmp) - 50; i++) {
        mg_strategy_t *s = &g_mg.strategies[i];
        pos += snprintf(tmp + pos, sizeof(tmp) - pos,
            "  %s: %d/%d (%.0f%%)\n",
            s->name, s->successes, s->attempts, s->success_rate * 100);
    }
    
    snprintf(buf, buf_size, "%s", tmp);
}

int mg_save(void) {
    if (!g_initialized) return -1;
    FILE *f = fopen("persist/mutation_guide.bin", "wb");
    if (!f) return -1;
    fwrite(&g_mg, sizeof(g_mg), 1, f);
    fclose(f);
    return 0;
}

int mg_load(void) {
    if (!g_initialized) mg_init();
    FILE *f = fopen("persist/mutation_guide.bin", "rb");
    if (!f) return -1;
    fread(&g_mg, sizeof(g_mg), 1, f);
    fclose(f);
    printf("  MGUIDE: loaded %d records (%.0f%% success rate)\n", 
           g_mg.total_attempts,
           g_mg.total_attempts > 0 ? 
               (float)g_mg.total_successes / g_mg.total_attempts * 100 : 0);
    return 0;
}
