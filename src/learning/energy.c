#include "energy.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── 全局 ──────────────────────────────────────────────────── */
static energy_manager_t g_eng;
static int g_initialized = 0;

/* ── 初始化 ───────────────────────────────────────────────── */
void eng_init(void) {
    memset(&g_eng, 0, sizeof(g_eng));
    g_eng.mode = ENERGY_MODE_BALANCED;
    g_eng.adjust_interval = 100;   /* 每100tick检查一次 */
    g_eng.baseline_cpu_load = 30.0f;
    g_eng.baseline_mem_kb = 4096.0f;
    g_initialized = 1;
    printf("  ENG: energy manager initialized (mode=balanced)\\n");
}

/* ── 设置模式 ──────────────────────────────────────────────── */
void eng_set_mode(energy_mode_t mode) {
    if (!g_initialized) return;
    g_eng.mode = mode;
    
    switch (mode) {
        case ENERGY_MODE_ECONOMY:
            g_eng.reduce_idle_freq = 1;
            g_eng.limit_branch_count = 1;
            g_eng.slow_heartbeat = 1;
            g_eng.throttle_level = 8;
            printf("  ENG: mode → ECONOMY (throttle=%d)\\n", g_eng.throttle_level);
            break;
        case ENERGY_MODE_BALANCED:
            g_eng.reduce_idle_freq = 0;
            g_eng.limit_branch_count = 0;
            g_eng.slow_heartbeat = 0;
            g_eng.throttle_level = 3;
            printf("  ENG: mode → BALANCED\\n");
            break;
        case ENERGY_MODE_PERFORMANCE:
            g_eng.reduce_idle_freq = 0;
            g_eng.limit_branch_count = 0;
            g_eng.slow_heartbeat = 0;
            g_eng.throttle_level = 0;
            printf("  ENG: mode → PERFORMANCE\\n");
            break;
        case ENERGY_MODE_COVERT:
            g_eng.reduce_idle_freq = 1;
            g_eng.limit_branch_count = 1;
            g_eng.slow_heartbeat = 1;
            g_eng.throttle_level = 10;
            printf("  ENG: mode → COVERT (throttle=%d)\\n", g_eng.throttle_level);
            break;
    }
}

/* ── 获取当前模式 ──────────────────────────────────────────── */
energy_mode_t eng_get_mode(void) {
    if (!g_initialized) return ENERGY_MODE_BALANCED;
    return g_eng.mode;
}

/* ── 更新资源统计 ──────────────────────────────────────────── */
void eng_update(float cpu_load, float mem_kb, float disk_io, uint8_t is_idle) {
    if (!g_initialized) return;
    
    /* 指数移动平均 (平滑, 降低噪声) */
    float alpha = 0.1f;
    g_eng.stats.cpu_load_avg = g_eng.stats.cpu_load_avg * (1.0f - alpha) + cpu_load * alpha;
    if (cpu_load > g_eng.stats.cpu_load_peak) g_eng.stats.cpu_load_peak = cpu_load;
    
    g_eng.stats.mem_usage_kb = g_eng.stats.mem_usage_kb * (1.0f - alpha) + mem_kb * alpha;
    g_eng.stats.disk_io_per_sec = g_eng.stats.disk_io_per_sec * (1.0f - alpha) + disk_io * alpha;
    
    g_eng.stats.total_ticks++;
    if (is_idle) g_eng.stats.idle_ticks++;
}

/* ── 自动调节 ──────────────────────────────────────────────── */
float eng_auto_adjust(uint64_t current_tick, uint8_t hw_stress) {
    if (!g_initialized) return 1.0f;
    if (current_tick - g_eng.last_adjust_tick < g_eng.adjust_interval) return 1.0f;
    
    g_eng.last_adjust_tick = (uint32_t)current_tick;
    
    float multiplier = 1.0f;
    
    switch (g_eng.mode) {
        case ENERGY_MODE_ECONOMY:
            multiplier = 4.0f;  /* 心跳慢4倍 */
            break;
        case ENERGY_MODE_BALANCED:
            /* 根据CPU负载动态调整 */
            if (g_eng.stats.cpu_load_avg > 60.0f) {
                multiplier = 1.5f;   /* 负载高, 慢一点 */
                if (g_eng.throttle_level < 6) g_eng.throttle_level++;
            } else if (g_eng.stats.cpu_load_avg < 15.0f) {
                multiplier = 0.8f;   /* 负载低, 可以快一点 */
                if (g_eng.throttle_level > 1) g_eng.throttle_level--;
            }
            
            /* 温度过高时自动降频 */
            if (hw_stress >= 2) {
                multiplier *= 1.5f;
                printf("  ENG: thermal throttle (stress=%d, mult=%.1f)\\n", hw_stress, multiplier);
            }
            break;
        case ENERGY_MODE_PERFORMANCE:
            multiplier = 0.5f;  /* 心跳快1倍 */
            g_eng.throttle_level = 0;
            break;
        case ENERGY_MODE_COVERT:
            multiplier = 8.0f;  /* 心跳慢8倍, 几乎不可见 */
            break;
    }
    
    /* 限制范围 */
    if (multiplier < 0.25f) multiplier = 0.25f;
    if (multiplier > 8.0f) multiplier = 8.0f;
    
    return multiplier;
}

/* ── 获取节流级别 ──────────────────────────────────────────── */
int8_t eng_throttle(void) {
    if (!g_initialized) return 0;
    return g_eng.throttle_level;
}

/* ── 是否限制分支 ──────────────────────────────────────────── */
uint8_t eng_should_limit_branches(void) {
    if (!g_initialized) return 0;
    return g_eng.limit_branch_count || (g_eng.stats.cpu_load_avg > 70.0f);
}

/* ── 打印能耗报告 ──────────────────────────────────────────── */
void eng_print_report(void) {
    if (!g_initialized) return;
    
    printf("  ENG: energy report (mode=%d)\\n", g_eng.mode);
    printf("       cpu_load: avg=%.1f%% peak=%.0f%%\\n",
           g_eng.stats.cpu_load_avg, g_eng.stats.cpu_load_peak);
    printf("       memory: %.0f KB\\n", g_eng.stats.mem_usage_kb);
    printf("       disk_io: %.1f ops/sec\\n", g_eng.stats.disk_io_per_sec);
    printf("       throttling: level=%d\\n", g_eng.throttle_level);
    printf("       total_ticks=%u, idle_ratio=%.0f%%\\n",
           g_eng.stats.total_ticks,
           g_eng.stats.total_ticks > 0 ?
           (float)g_eng.stats.idle_ticks / g_eng.stats.total_ticks * 100.0f : 0.0f);
}
