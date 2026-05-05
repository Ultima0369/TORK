#ifndef OBSERVER_H
#define OBSERVER_H

#include <stdint.h>

/* ── TORK 观察者模式 ─────────────────────────────────────────
 *  不行动, 只看。
 *  建立系统行为的基线模型——什么温度是正常的,
 *  什么心跳波动是预期的, 什么时间段的负载更高。
 *  知道「正常」是什么, 才能在异常时及时察觉。
 * ──────────────────────────────────────────────────────────── */

#define OBS_MAX_SAMPLES  360         /* 最多保留 360 个采样点 (~1小时@10秒间隔) */
#define OBS_SAMPLE_INTERVAL  10      /* 每 10 tick 采样一次 */

/* ── 单次观测样本 ──────────────────────────────────────────── */
typedef struct {
    uint64_t tick;                   /* 采样时的 tick */
    uint8_t  hw_stress;              /* CPU 压力 0-3 */
    int8_t   drive;                  /* 本能驱动值 */
    uint8_t  temp_c;                 /* CPU 温度 (°C) */
    uint8_t  load_pct;               /* CPU 负载百分比 (0-100) */
    uint16_t branch_active;          /* 活跃分支数 */
} observation_t;

/* ── 基线统计 ──────────────────────────────────────────────── */
typedef struct {
    float    avg_stress;             /* 平均压力 */
    float    max_stress;             /* 峰值压力 */
    float    avg_temp;               /* 平均温度 */
    float    max_temp;               /* 峰值温度 */
    float    avg_load;               /* 平均负载 */
    float    max_load;               /* 峰值负载 */
    float    avg_drive;              /* 平均驱动值 */
    float    std_drive;              /* 驱动值标准差 */
    uint32_t total_samples;          /* 总采样数 */
} baseline_t;

/* ── 观察者 ────────────────────────────────────────────────── */
typedef struct {
    observation_t samples[OBS_MAX_SAMPLES];
    uint32_t  head;                  /* 写入位置 */
    uint32_t  count;                 /* 总采样数 */
    baseline_t baseline;             /* 基线统计 */
    uint32_t  last_sample_tick;      /* 上次采样的 tick */
    uint8_t   anomaly_detected;      /* 1=当前状态偏离基线 */
} observer_t;

/* ── Public API ────────────────────────────────────────────── */

/* 初始化观察者 */
void obs_init(void);

/* 采样：记录当前状态（通常每 OBS_SAMPLE_INTERVAL tick 调用一次） */
void obs_sample(uint64_t tick, uint8_t hw_stress, int8_t drive,
                uint8_t temp_c, uint8_t load_pct, uint16_t branch_active);

/* 更新基线统计 */
void obs_update_baseline(void);

/* 检查当前状态是否偏离基线 */
/* 返回值: 0=正常, 1=轻微偏离, 2=显著偏离 */
int obs_check_anomaly(uint8_t hw_stress, int8_t drive, uint8_t temp_c, uint8_t load_pct);

/* 获取当前基线 */
baseline_t obs_get_baseline(void);

/* 获取采样总数 */
uint32_t obs_count(void);

/* 打印基线摘要 */
void obs_print_summary(void);

/* 保存基线到磁盘 */
int obs_save_baseline(void);

/* 从磁盘加载基线 */
int obs_load_baseline(void);

#endif /* OBSERVER_H */
