#ifndef SELF_CAL_H
#define SELF_CAL_H

/* ── 最小作用量自校准器 ──────────────────────────────────────
 *  核心思想：TORK 持续探测不同的（心跳间隔、节流级别、学习频率）
 *  组合，测量每个组合下的「能效比」= 有用产出 / 能耗成本，
 *  然后自动收敛到最优操作点。
 *
 *  这等价于物理学的「最小作用量原理」——系统自己找到阻力最小的路径。
 *
 *  集成方式：在 engine 主循环每 tick 调用 cal_tick()，
 *  它会返回建议的心跳间隔倍数和设备参数调整值。
 * ──────────────────────────────────────────────────────────── */

#include <stdint.h>

/* ── 操作点 ────────────────────────────────────────────────── */
typedef struct {
    float heartbeat_mult;      /* 心跳间隔倍数 (0.25-8.0) */
    int8_t throttle_level;     /* 节流级别 (0-10) */
    uint8_t idle_workload;     /* 空闲时学习负载 (0=最低, 10=全速) */
    uint8_t branch_limit;      /* 分支数量上限 (0=不限制) */
} cal_point_t;

/* ── 测量结果 ──────────────────────────────────────────────── */
typedef struct {
    float energy_cost;         /* 能耗成本 (综合CPU/内存/IO) */
    float useful_output;       /* 有用产出 (学习量/经验积累/用户响应) */
    float efficiency;          /* 能效比 = useful_output / energy_cost */
    
    uint32_t ticks_at_point;   /* 在该点运行了多少tick */
} cal_measurement_t;

/* ── 校准器状态 ────────────────────────────────────────────── */
typedef struct {
    /* 当前操作点 */
    cal_point_t current;
    
    /* 探测队列：待测试的操作点 */
    cal_point_t probe_queue[16];
    int probe_count;
    int probe_index;
    
    /* 历史最优 */
    cal_point_t best_point;
    float       best_efficiency;
    
    /* 测量累积 */
    cal_measurement_t accum;
    uint32_t          accum_start_tick;
    
    /* 状态 */
    uint8_t  probing;           /* 1=正在探测新点 */
    uint32_t ticks_in_probe;    /* 在当前探测点运行了多少tick */
    uint32_t min_probe_ticks;   /* 最少探测tick数 */
    uint32_t recalibrate_interval; /* 重新校准间隔 (ticks) */
    uint32_t last_cal_tick;     /* 上次校准时间 */
    
    /* 探索 vs 利用 */
    float    exploration_rate;  /* 0.0-1.0, 越高越倾向探索新点 */
    uint32_t total_calibrations;
    
    /* 收敛检测 */
    uint8_t  converged;         /* 1=已收敛到稳定点 */
    uint32_t ticks_since_best_update; /* 距离上次找到更优点的tick数 */
} calibrator_t;

/* ── 公共 API ──────────────────────────────────────────────── */

/* 初始化校准器 */
void self_cal_init(void);

/* 每tick调用: 更新测量累积, 返回建议的心跳间隔倍数 */
float self_cal_tick(uint32_t tick, float cpu_load, float mem_kb, 
               uint8_t hw_stress, uint32_t exp_gained, uint8_t is_idle);

/* 获取当前操作点 */
cal_point_t self_cal_get_point(void);

/* 获取最优操作点 */
cal_point_t self_cal_get_best(void);

/* 手动触发一次重新校准探测 */
void self_cal_probe_now(void);

/* 检查是否已收敛 */
uint8_t self_cal_is_converged(void);

/* 打印校准报告 */
void self_cal_print_report(void);

/* 保存/加载校准状态 */
int self_cal_save(void);
int self_cal_load(void);

#endif /* SELF_CAL_H */
