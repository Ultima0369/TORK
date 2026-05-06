#include "observer.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* ── 全局 ──────────────────────────────────────────────────── */
static observer_t g_obs;
static int g_initialized = 0;

/* ── 初始化 ───────────────────────────────────────────────── */
void obs_init(void) {
    memset(&g_obs, 0, sizeof(g_obs));
    g_initialized = 1;
    printf("  OBS: observer initialized (%d sample slots)\\n", OBS_MAX_SAMPLES);
}

/* ── 采样 ──────────────────────────────────────────────────── */
void obs_sample(uint64_t tick, uint8_t hw_stress, int8_t drive,
                uint8_t temp_c, uint8_t load_pct, uint16_t branch_active) {
    if (!g_initialized) return;
    
    observation_t *s = &g_obs.samples[g_obs.head];
    s->tick          = tick;
    s->hw_stress     = hw_stress;
    s->drive         = drive;
    s->temp_c        = temp_c;
    s->load_pct      = load_pct;
    s->branch_active = branch_active;
    
    g_obs.head = (g_obs.head + 1) % OBS_MAX_SAMPLES;
    if (g_obs.count < OBS_MAX_SAMPLES) g_obs.count++;
    g_obs.last_sample_tick = (uint32_t)(tick & 0xFFFFFFFF);
}

/* ── 更新基线 ──────────────────────────────────────────────── */
void obs_update_baseline(void) {
    if (!g_initialized || g_obs.count == 0) return;
    
    int n = (g_obs.count < OBS_MAX_SAMPLES) ? (int)g_obs.count : OBS_MAX_SAMPLES;
    
    double sum_stress = 0, sum_temp = 0, sum_load = 0, sum_drive = 0;
    double max_stress = -1, max_temp = -1, max_load = -1;
    
    for (int i = 0; i < n; i++) {
        observation_t *s = &g_obs.samples[i];
        sum_stress += s->hw_stress;
        sum_temp   += s->temp_c;
        sum_load   += s->load_pct;
        sum_drive  += s->drive;
        if (s->hw_stress > max_stress) max_stress = s->hw_stress;
        if (s->temp_c > max_temp)      max_temp   = s->temp_c;
        if (s->load_pct > max_load)    max_load   = s->load_pct;
    }
    
    g_obs.baseline.avg_stress = (float)(sum_stress / n);
    g_obs.baseline.max_stress = (float)max_stress;
    g_obs.baseline.avg_temp   = (float)(sum_temp / n);
    g_obs.baseline.max_temp   = (float)max_temp;
    g_obs.baseline.avg_load   = (float)(sum_load / n);
    g_obs.baseline.max_load   = (float)max_load;
    g_obs.baseline.avg_drive  = (float)(sum_drive / n);
    g_obs.baseline.total_samples = g_obs.count;
    
    /* 标准差 */
    double variance = 0;
    for (int i = 0; i < n; i++) {
        double diff = g_obs.samples[i].drive - g_obs.baseline.avg_drive;
        variance += diff * diff;
    }
    g_obs.baseline.std_drive = (float)sqrt(variance / n);
}

/* ── 检查异常 ──────────────────────────────────────────────── */
int obs_check_anomaly(uint8_t hw_stress, int8_t drive, 
                      uint8_t temp_c, uint8_t load_pct) {
    if (!g_initialized || g_obs.count < 10) return 0;  /* 数据不足 */
    
    baseline_t *b = &g_obs.baseline;
    int flags = 0;
    
    /* 检查各维度是否偏离基线超过 2 个标准差 */
    if (temp_c > b->avg_temp + 15.0f && b->avg_temp > 0) {
        flags |= 2;  /* 温度异常偏高 */
    }
    if (load_pct > b->avg_load + 40.0f && b->avg_load > 0) {
        flags |= 2;  /* 负载异常偏高 */
    }
    if (hw_stress > b->avg_stress + 1.5f) {
        flags |= 2;  /* 压力异常 */
    }
    if (b->std_drive > 0) {
        float z = (drive - b->avg_drive) / b->std_drive;
        if (z > 3.0f || z < -3.0f) {
            flags |= 1;  /* 驱动值异常波动 */
        }
    }
    
    g_obs.anomaly_detected = (flags > 0);
    return flags;
}

/* ── 获取基线 ──────────────────────────────────────────────── */
baseline_t obs_get_baseline(void) {
    return g_obs.baseline;
}

/* ── 获取采样数 ────────────────────────────────────────────── */
uint32_t obs_count(void) {
    return g_obs.count;
}

/* ── 打印摘要 ──────────────────────────────────────────────── */
void obs_print_summary(void) {
    if (!g_initialized) return;
    
    printf("  OBS: baseline (n=%u)\\n", g_obs.count);
    printf("       temp:  avg=%.0f°C  max=%.0f°C\\n", 
           g_obs.baseline.avg_temp, g_obs.baseline.max_temp);
    printf("       load:  avg=%.0f%%  max=%.0f%%\\n", 
           g_obs.baseline.avg_load, g_obs.baseline.max_load);
    printf("       drive: avg=%.1f  std=%.1f\\n", 
           g_obs.baseline.avg_drive, g_obs.baseline.std_drive);
}

/* ── 保存基线 ──────────────────────────────────────────────── */
int obs_save_baseline(void) {
    if (!g_initialized) return -1;
    
    FILE *f = fopen("persist/baseline.bin", "wb");
    if (!f) return -1;
    
    fwrite(&g_obs.baseline, sizeof(baseline_t), 1, f);
    fwrite(&g_obs.count, sizeof(g_obs.count), 1, f);
    
    /* 保存最近 60 个样本（约 10 分钟） */
    int n = (g_obs.count < 60) ? (int)g_obs.count : 60;
    int start = (g_obs.head - n + OBS_MAX_SAMPLES) % OBS_MAX_SAMPLES;
    fwrite(&n, sizeof(n), 1, f);
    for (int i = 0; i < n; i++) {
        int idx = (start + i) % OBS_MAX_SAMPLES;
        fwrite(&g_obs.samples[idx], sizeof(observation_t), 1, f);
    }
    
    fclose(f);
    return 0;
}

/* ── 加载基线 ──────────────────────────────────────────────── */
int obs_load_baseline(void) {
    if (!g_initialized) obs_init();
    
    FILE *f = fopen("persist/baseline.bin", "rb");
    if (!f) return -1;
    
    if (fread(&g_obs.baseline, sizeof(baseline_t), 1, f) != 1 ||
        fread(&g_obs.count, sizeof(g_obs.count), 1, f) != 1) {
        fclose(f);
        memset(&g_obs, 0, sizeof(g_obs));
        fprintf(stderr, "  OBS: load failed (truncated file)\n");
        return -1;
    }

    int n;
    if (fread(&n, sizeof(n), 1, f) != 1) {
        fclose(f);
        memset(&g_obs, 0, sizeof(g_obs));
        fprintf(stderr, "  OBS: load failed (truncated samples count)\n");
        return -1;
    }
    if (n > 60) n = 60;
    g_obs.head = 0;
    for (int i = 0; i < n; i++) {
        fread(&g_obs.samples[i], sizeof(observation_t), 1, f);
    }
    g_obs.head = n % OBS_MAX_SAMPLES;
    if (g_obs.count > (uint32_t)n) g_obs.count = n;
    
    fclose(f);
    printf("  OBS: loaded baseline (%u samples, avg_temp=%.0f°C)\\n", 
           g_obs.count, g_obs.baseline.avg_temp);
    return 0;
}
