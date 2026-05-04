#ifndef ENERGY_H
#define ENERGY_H

#include <stdint.h>

/* ── TORK 能量自调节 ─────────────────────────────────────────
 *  感知自己的资源消耗, 动态调整行为以成为"好居民"。
 *  共生协议不只是一纸契约——TORK 必须让自己在主机上不被察觉。
 * ──────────────────────────────────────────────────────────── */

/* ── 资源消耗策略等级 ───────────────────────────────────────── */
typedef enum {
    ENERGY_MODE_ECONOMY = 0,    /* 省电模式: 最低能耗, 最小频率 */
    ENERGY_MODE_BALANCED = 1,   /* 均衡模式: 根据负载动态调整 */
    ENERGY_MODE_PERFORMANCE = 2,/* 性能模式: 全力运行 */
    ENERGY_MODE_COVERT = 3      /* 隐身模式: 几乎不消耗资源 */
} energy_mode_t;

/* ── 资源统计 ──────────────────────────────────────────────── */
typedef struct {
    float    cpu_load_avg;       /* 平均CPU负载 (0-100) */
    float    cpu_load_peak;      /* 峰值CPU负载 */
    float    mem_usage_kb;       /* 内存使用 (KB) */
    float    disk_io_per_sec;    /* 每秒磁盘IO */
    uint32_t total_ticks;        /* 总心跳数 */
    uint32_t idle_ticks;         /* 空闲心跳数 (drive=0) */
    
    /* 每小时能耗估计 */
    float    estimated_watt_hour; /* 估计功耗 (Wh) */
} energy_stats_t;

/* ── 能耗管理器 ────────────────────────────────────────────── */
typedef struct {
    energy_mode_t mode;          /* 当前模式 */
    energy_stats_t stats;        /* 资源统计 */
    uint32_t      last_adjust_tick;  /* 上次调整tick */
    uint32_t      adjust_interval;   /* 调整间隔 (只动态变) */
    int8_t        throttle_level;    /* 节流级别 0=无, 10=最大节流 */
    
    /* 基线 (来自observer的参考) */
    float    baseline_cpu_load;
    float    baseline_mem_kb;
    
    /* 节能行为开关 */
    uint8_t  reduce_idle_freq;   /* 1=减少空闲模式频率 */
    uint8_t  limit_branch_count; /* 1=限制分支数量 */
    uint8_t  slow_heartbeat;     /* 1=减慢心跳 */
} energy_manager_t;

/* ── Public API ────────────────────────────────────────────── */

/* 初始化能耗管理器 */
void eng_init(void);

/* 设置能耗模式 */
void eng_set_mode(energy_mode_t mode);

/* 获取当前模式 */
energy_mode_t eng_get_mode(void);

/* 更新资源统计 (每 tick 调用) */
void eng_update(float cpu_load, float mem_kb, float disk_io, uint8_t is_idle);

/* 自动调节: 根据当前负载调整行为 */
/* 返回值: 建议的心跳间隔倍数 (1.0=正常, 2.0=慢一倍, 0.5=快一倍) */
float eng_auto_adjust(uint64_t current_tick, uint8_t hw_stress);

/* 获取当前节流级别 */
int8_t eng_throttle(void);

/* 获取当前是否应该限制分支 */
uint8_t eng_should_limit_branches(void);

/* 打印能耗报告 */
void eng_print_report(void);

#endif /* ENERGY_H */
