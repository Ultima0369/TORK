#include "self_cal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define CAL_PATH "persist/calibrator.bin"

static calibrator_t g_cal;
static int g_initialized = 0;

/* ── 生成默认探测队列 ──────────────────────────────────────── */
/* 覆盖 (心跳, 节流, 空闲负载) 三维空间中的关键点 */
static void generate_probe_queue(void) {
    cal_point_t probes[] = {
        /*  心跳乘数, 节流, 空闲负载, 分支上限 */
        { 1.0f,  0, 5, 0 },   /* 平衡点 */
        { 0.5f,  0, 8, 0 },   /* 激进(快心跳+高负载) */
        { 2.0f,  3, 5, 4 },   /* 保守(慢心跳+节流) */
        { 4.0f,  6, 3, 2 },   /* 省电(很慢+高节流) */
        { 1.5f,  2, 6, 6 },   /* 轻度保守 */
        { 0.8f,  1, 7, 0 },   /* 轻度激进 */
        { 3.0f,  4, 4, 3 },   /* 中度省电 */
        { 6.0f,  8, 2, 1 },   /* 极限省电 */
        { 1.2f,  1, 6, 0 },   /* 接近平衡偏快 */
        { 2.5f,  5, 4, 4 },   /* 中等保守 */
        { 0.6f,  0, 9, 0 },   /* 全速 */
        { 5.0f,  7, 3, 2 },   /* 深度省电 */
        { 1.8f,  3, 5, 5 },   /* 平衡偏保守 */
        { 0.9f,  0, 7, 0 },   /* 平衡偏快 */
        { 8.0f,  10, 1, 1 },  /* 隐身(最慢+最高节流) */
        { 0.4f,  0, 10, 0 },  /* 极限性能 */
    };
    
    int n = sizeof(probes) / sizeof(probes[0]);
    g_cal.probe_count = n;
    
    /* 从当前位置开始偏移打乱顺序 (避免总是从同一个点开始) */
    int start = (int)(g_cal.total_calibrations % n);
    for (int i = 0; i < n; i++) {
        g_cal.probe_queue[i] = probes[(start + i) % n];
    }
    g_cal.probe_index = 0;
}

/* ── 在最优解附近生成精细探测点 ───────────────────────────── */
static void generate_fine_probes(void) {
    cal_point_t base = g_cal.best_point;
    int n = 0;
    
    /* 在最优解 ±20% 范围内生成 8 个精细探测点 */
    float heart_rates[] = {base.heartbeat_mult * 0.8f, base.heartbeat_mult * 0.9f,
                           base.heartbeat_mult * 1.0f, base.heartbeat_mult * 1.1f,
                           base.heartbeat_mult * 1.2f, base.heartbeat_mult * 0.85f,
                           base.heartbeat_mult * 0.95f, base.heartbeat_mult * 1.15f};
    int8_t throttles[] = {base.throttle_level, base.throttle_level,
                          base.throttle_level - 1, base.throttle_level + 1,
                          base.throttle_level, base.throttle_level - 1,
                          base.throttle_level + 1, base.throttle_level};
    uint8_t loads[] = {base.idle_workload, base.idle_workload + 1,
                       base.idle_workload, base.idle_workload - 1,
                       base.idle_workload, base.idle_workload + 1,
                       base.idle_workload - 1, base.idle_workload};
    
    for (int i = 0; i < 8 && i < 16; i++) {
        g_cal.probe_queue[i] = (cal_point_t){
            fminf(8.0f, fmaxf(0.25f, heart_rates[i])),
            (int8_t)fminf(10, fmaxf(0, throttles[i])),
            (uint8_t)fminf(10, fmaxf(0, loads[i])),
            base.branch_limit
        };
        n++;
    }
    g_cal.probe_count = n;
    g_cal.probe_index = 0;
}

/* ── 初始化 ────────────────────────────────────────────────── */
void self_cal_init(void) {
    memset(&g_cal, 0, sizeof(g_cal));
    
    /* 默认操作点: 平衡 */
    g_cal.current = (cal_point_t){1.0f, 3, 5, 0};
    g_cal.best_point = g_cal.current;
    g_cal.best_efficiency = 0.0f;
    g_cal.exploration_rate = 0.3f;
    g_cal.min_probe_ticks = 200;   /* 每个点至少跑 200 tick */
    g_cal.recalibrate_interval = 5000;  /* 每 5000 tick 重新校准一次 */
    g_cal.ticks_in_probe = 0;
    g_cal.converged = 0;
    
    /* 尝试加载已保存的校准状态 */
    if (self_cal_load() == 0) {
        printf("  CAL: loaded saved calibration (best_efficiency=%.2f)\n",
               g_cal.best_efficiency);
    } else {
        /* 首次运行: 生成探测队列 */
        generate_probe_queue();
        g_cal.probing = 1;
        g_cal.current = g_cal.probe_queue[0];
        printf("  CAL: calibrator initialized (probing %d points)\n",
               g_cal.probe_count);
    }
    
    g_cal.accum_start_tick = 0;
    memset(&g_cal.accum, 0, sizeof(g_cal.accum));
    g_initialized = 1;
}

/* ── 开始探测下一个点 ──────────────────────────────────────── */
static void start_next_probe(uint32_t tick) {
    if (g_cal.probe_index >= g_cal.probe_count) {
        /* 所有点探测完毕: 检查收敛性 */
        if (g_cal.total_calibrations < 3) {
            /* 前3轮全量探测 */
            generate_probe_queue();
        } else {
            /* 后续轮次: 在最优解附近精细探测 */
            generate_fine_probes();
        }
    }
    
    /* 切换到下一个探测点 */
    g_cal.current = g_cal.probe_queue[g_cal.probe_index % g_cal.probe_count];
    g_cal.probe_index++;
    g_cal.ticks_in_probe = 0;
    g_cal.accum_start_tick = tick;
    memset(&g_cal.accum, 0, sizeof(g_cal.accum));
}

/* ── 计算能效比 ────────────────────────────────────────────── */
static float calculate_efficiency(float energy_cost, float useful_output) {
    if (energy_cost < 0.001f) return 0.0f;
    return useful_output / energy_cost;
}

/* ── 评估当前探测点 ────────────────────────────────────────── */
static void evaluate_current_point(uint32_t tick) {
    cal_measurement_t *m = &g_cal.accum;
    
    if (m->ticks_at_point < g_cal.min_probe_ticks) return;
    if (m->energy_cost < 0.01f) return;
    
    m->efficiency = calculate_efficiency(m->energy_cost, m->useful_output);
    
    /* 更新最优 */
    if (m->efficiency > g_cal.best_efficiency) {
        g_cal.best_efficiency = m->efficiency;
        g_cal.best_point = g_cal.current;
        g_cal.ticks_since_best_update = 0;
        
        printf("  CAL: new best point "
               "(hb=%.1fx, throttle=%d, idle=%d, eff=%.2f)\n",
               g_cal.best_point.heartbeat_mult,
               g_cal.best_point.throttle_level,
               g_cal.best_point.idle_workload,
               g_cal.best_efficiency);
    } else {
        g_cal.ticks_since_best_update += m->ticks_at_point;
    }
    
    /* 收敛检测: 如果很久没找到更优解, 标记为收敛 */
    if (g_cal.ticks_since_best_update > g_cal.recalibrate_interval * 2) {
        if (!g_cal.converged) {
            g_cal.converged = 1;
            printf("  CAL: converged to hb=%.1fx, throttle=%d, idle=%d "
                   "(eff=%.2f)\n",
                   g_cal.best_point.heartbeat_mult,
                   g_cal.best_point.throttle_level,
                   g_cal.best_point.idle_workload,
                   g_cal.best_efficiency);
        }
    }
}

/* ── 每 tick 调用 ──────────────────────────────────────────── */
float self_cal_tick(uint32_t tick, float cpu_load, float mem_kb,
               uint8_t hw_stress, uint32_t exp_gained, uint8_t is_idle) {
    if (!g_initialized) return 1.0f;
    
    /* ── 累积测量数据 ── */
    g_cal.accum.ticks_at_point++;
    
    /* 能耗成本 = CPU负载 × 权重 + 内存 × 权重 + 应力惩罚 */
    float energy_cost = cpu_load * 1.0f;
    energy_cost += mem_kb / 10000.0f;   /* 10MB ≈ 1点 */
    if (hw_stress >= 2) energy_cost *= 1.5f;  /* 高温惩罚 */
    if (is_idle) energy_cost *= 0.3f;   /* 空闲时减权 */
    
    g_cal.accum.energy_cost += energy_cost;
    g_cal.accum.useful_output += (float)exp_gained * 10.0f;
    if (!is_idle) g_cal.accum.useful_output += 1.0f;  /* 活跃本身有价值 */
    
    g_cal.ticks_in_probe++;
    
    /* ── 判断是否需要切换到下一个探测点 ── */
    uint32_t probe_duration = g_cal.ticks_in_probe;
    int should_switch = 0;
    
    if (probe_duration >= g_cal.min_probe_ticks) {
        /* 最小探测时间够了 */
        if (g_cal.probing) {
            /* 正处在探测模式: 累积足够数据后切换 */
            if (g_cal.accum.ticks_at_point >= g_cal.min_probe_ticks) {
                should_switch = 1;
            }
        }
        
        /* 如果当前点导致高应力, 加速切换 */
        if (hw_stress >= 3 && probe_duration > 50) {
            should_switch = 1;
        }
    }
    
    if (should_switch) {
        evaluate_current_point(tick);
        start_next_probe(tick);
    }
    
    /* ── 定期重新校准 ── */
    if (tick - g_cal.last_cal_tick >= g_cal.recalibrate_interval) {
        g_cal.last_cal_tick = tick;
        g_cal.total_calibrations++;
        
        if (!g_cal.probing) {
            /* 进入新一轮探测 */
            g_cal.probing = 1;
            g_cal.converged = 0;
            
            /* 降低探索率 (随着时间推移更稳定) */
            g_cal.exploration_rate *= 0.95f;
            if (g_cal.exploration_rate < 0.05f) g_cal.exploration_rate = 0.05f;
            
            /* 有一定概率在当前最优解附近精细探测, 而不是全局探测 */
            if (g_cal.best_efficiency > 0 && (rand() % 100) < 80) {
                generate_fine_probes();
                printf("  CAL: recalibrating (fine-tune around best, round %d)\n",
                       g_cal.total_calibrations);
            } else {
                generate_probe_queue();
                printf("  CAL: recalibrating (full sweep, round %d)\n",
                       g_cal.total_calibrations);
            }
            
            g_cal.current = g_cal.probe_queue[0];
            g_cal.ticks_in_probe = 0;
            g_cal.accum_start_tick = tick;
            memset(&g_cal.accum, 0, sizeof(g_cal.accum));
        }
        
        /* 自动保存校准状态 */
        self_cal_save();
    }
    
    /* ── 如果收敛了, 使用最优解; 否则使用当前探测点 ── */
    if (g_cal.converged && !g_cal.probing) {
        return g_cal.best_point.heartbeat_mult;
    }
    
    return g_cal.current.heartbeat_mult;
}

/* ── 获取当前操作点 ────────────────────────────────────────── */
cal_point_t self_cal_get_point(void) {
    return g_cal.current;
}

/* ── 获取最优操作点 ────────────────────────────────────────── */
cal_point_t self_cal_get_best(void) {
    return g_cal.best_point;
}

/* ── 手动触发重新校准 ──────────────────────────────────────── */
void self_cal_probe_now(void) {
    if (!g_initialized) return;
    g_cal.probing = 1;
    g_cal.converged = 0;
    g_cal.last_cal_tick = 0;  /* 强制下次tick触发校准 */
    printf("  CAL: manual recalibration triggered\n");
}

/* ── 检查是否已收敛 ────────────────────────────────────────── */
uint8_t self_cal_is_converged(void) {
    if (!g_initialized) {
        return 0;
    }
    if (g_cal.converged && !g_cal.probing) {
        return 1;
    }
    return 0;
}

/* ── 打印校准报告 ──────────────────────────────────────────── */
void self_cal_print_report(void) {
    if (!g_initialized) return;
    
    printf("  CAL: self-calibration report\n");
    printf("       current: hb=%.1fx throttle=%d idle=%d\n",
           g_cal.current.heartbeat_mult, g_cal.current.throttle_level,
           g_cal.current.idle_workload);
    printf("       best:    hb=%.1fx throttle=%d idle=%d (eff=%.2f)\n",
           g_cal.best_point.heartbeat_mult, g_cal.best_point.throttle_level,
           g_cal.best_point.idle_workload, g_cal.best_efficiency);
    printf("       total calibrations: %d | exploration: %.0f%%\n",
           g_cal.total_calibrations, g_cal.exploration_rate * 100.0f);
    printf("       state: %s\n",
           g_cal.converged ? (g_cal.probing ? "recalibrating" : "converged ✅")
                           : (g_cal.probing ? "probing" : "idle"));
}

/* ── 保存/加载 ──────────────────────────────────────────────── */
int self_cal_save(void) {
    if (!g_initialized) return -1;
    FILE *f = fopen(CAL_PATH, "wb");
    if (!f) return -1;
    size_t written = fwrite(&g_cal, 1, sizeof(g_cal), f);
    fclose(f);
    return (written == sizeof(g_cal)) ? 0 : -1;
}

int self_cal_load(void) {
    FILE *f = fopen(CAL_PATH, "rb");
    if (!f) return -1;
    size_t read = fread(&g_cal, 1, sizeof(g_cal), f);
    fclose(f);
    if (read != sizeof(g_cal)) return -1;
    
    /* 确保探测队列有效 */
    g_cal.probe_index = 0;
    if (g_cal.probe_count <= 0 || g_cal.probe_count > 16) {
        generate_probe_queue();
    }
    
    g_initialized = 1;
    return 0;
}
