#include "self_tune.h"
#include "pattern.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── 全局状态 ─────────────────────────────────────────────── */
static tune_params_t g_params;
static int g_initialized = 0;
static int g_adjust_count = 0;
#define TUNE_PATH "persist/tune_params.bin"

/* 默认参数 */
static const tune_params_t DEFAULT_PARAMS = {
    .fear_weight      = 1.0f,
    .desire_weight    = 1.0f,
    .curiosity_weight = 1.0f,
    .learning_rate    = 0.1f,
    .heartbeat_interval = 500,
    .exploration_rate = 20
};

void tune_init(float fear_base, float desire_base, float curiosity_base) {
    g_params = DEFAULT_PARAMS;
    /* Start from instinct's current params */
    g_params.fear_weight = fear_base;
    g_params.desire_weight = desire_base;
    g_params.curiosity_weight = curiosity_base;
    
    /* Try to load saved params (which overlay on top of instinct base) */
    FILE *f = fopen(TUNE_PATH, "rb");
    if (f) {
        tune_params_t loaded;
        if (fread(&loaded, sizeof(loaded), 1, f) == 1) {
            /* Loaded params are RELATIVE to instinct base */
            g_params.curiosity_weight = curiosity_base + (loaded.curiosity_weight - 1.0f);
            g_params.fear_weight = fear_base + (loaded.fear_weight - 1.0f);
            g_params.desire_weight = desire_base + (loaded.desire_weight - 1.0f);
            g_params.learning_rate = loaded.learning_rate;
            g_params.heartbeat_interval = loaded.heartbeat_interval;
            g_params.exploration_rate = loaded.exploration_rate;
            printf("  TUNE: loaded params (curiosity=%.2f on base %.2f)\n",
                   g_params.curiosity_weight, curiosity_base);
        }
        fclose(f);
    } else {
        printf("  TUNE: initialized from instinct (curiosity=%.2f)\n", curiosity_base);
    }
    
    g_initialized = 1;
}

void tune_adjust_from_patterns(void) {
    if (!g_initialized) return;
    
    int pattern_count = pat_count();
    if (pattern_count == 0) return;
    
    g_adjust_count++;
    
    /* ── Strategy: use pattern statistics to nudge parameters ── */
    
    /* 1. If many patterns exist with high avg_outcome, increase curiosity */
    /*    (patterns are working → explore more) */
    if (pattern_count >= 3) {
        g_params.curiosity_weight += 0.02f;
        if (g_params.curiosity_weight > 2.0f) g_params.curiosity_weight = 2.0f;
    }
    
    /* 2. Every 5 adjusts, nudge exploration rate up */
    if (g_adjust_count % 5 == 0) {
        g_params.exploration_rate += 2;
        if (g_params.exploration_rate > 50) g_params.exploration_rate = 50;
        g_params.learning_rate += 0.01f;
        if (g_params.learning_rate > 0.5f) g_params.learning_rate = 0.5f;
    }
    
    /* 3. Periodically nudge heartbeat faster (confidence grows) */
    if (g_adjust_count % 10 == 0) {
        g_params.heartbeat_interval -= 10;
        if (g_params.heartbeat_interval < 100) g_params.heartbeat_interval = 100;
    }
    
    /* Auto-save every 3 adjusts */
    if (g_adjust_count % 3 == 0) {
        tune_save();
    }
}

tune_params_t tune_get_params(void) {
    return g_params;
}

int tune_save(void) {
    if (!g_initialized) return -1;
    
    FILE *f = fopen(TUNE_PATH, "wb");
    if (!f) return -1;
    
    size_t wrote = fwrite(&g_params, sizeof(g_params), 1, f);
    fwrite(&g_adjust_count, sizeof(g_adjust_count), 1, f);
    fclose(f);
    
    if (wrote == 1) {
        printf("  TUNE: saved params (curiosity=%.2f, exploration=%d, hb=%dms)\n",
               g_params.curiosity_weight, g_params.exploration_rate, g_params.heartbeat_interval);
        return 0;
    }
    return -1;
}

int tune_load(void) {
    if (!g_initialized) return -1;
    
    FILE *f = fopen(TUNE_PATH, "rb");
    if (!f) return -1;
    
    tune_params_t loaded;
    if (fread(&loaded, sizeof(loaded), 1, f) == 1) {
        fread(&g_adjust_count, sizeof(g_adjust_count), 1, f);
        g_params = loaded;
        fclose(f);
        printf("  TUNE: loaded params (curiosity=%.2f, exploration=%d)\n",
               g_params.curiosity_weight, g_params.exploration_rate);
        return 0;
    }
    fclose(f);
    return -1;
}

void tune_print(void) {
    if (!g_initialized) return;
    printf("  TUNE: fear=%.2f desire=%.2f curiosity=%.2f | lr=%.2f hb=%dms explore=%d%% (adjusts=%d)\n",
           g_params.fear_weight, g_params.desire_weight, g_params.curiosity_weight,
           g_params.learning_rate, g_params.heartbeat_interval, g_params.exploration_rate,
           g_adjust_count);
}
