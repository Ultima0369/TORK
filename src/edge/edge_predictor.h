#ifndef EDGE_PREDICTOR_H
#define EDGE_PREDICTOR_H

/* ══════════════════════════════════════════════════════════════
 * TORK 边缘异常预测引擎
 *
 * 不是阈值触发，而是模式偏离检测。
 * TORK 的 TLN 持续学习"什么是正常"，然后感知"什么是异常"。
 *
 * 核心能力：
 * 1. 模式偏离检测 — "这个模式我没见过"
 * 2. 趋势提前预警 — "虽然还没到阈值，但趋势不对"
 * 3. 多传感器联合 — "温度+电压+电流的模式都不对"
 * ══════════════════════════════════════════════════════════════ */

#include <stdint.h>
#include "edge_sensor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 预警等级 ──────────────────────────────────────────── */
typedef enum {
    WARN_NONE      = 0,  /* 正常 */
    WARN_ATTENTION = 1,  /* 关注: 模式轻微偏离 */
    WARN_CAUTION   = 2,  /* 谨慎: 趋势持续恶化 */
    WARN_EARLY     = 3,  /* 早期预警: 超前 10-20 分钟 */
    WARN_CRITICAL  = 4,  /* 临界: 逼近危险区 */
} edge_warn_level_t;

/* ── 预警消息 ──────────────────────────────────────────── */
typedef struct {
    edge_warn_level_t level;
    uint32_t          timestamp_ms;
    char              message[128];      /* 人类可读 */
    uint8_t           lead_time_min;     /* 预估提前量 (分钟) */
    uint8_t           sensor_count;      /* 涉及的传感器数 */
    edge_sensor_type_t sensors[8];       /* 涉及的传感器 */
    uint16_t          confidence;        /* 置信度 0-1000 */
} edge_alert_t;

/* ── 趋势跟踪 ──────────────────────────────────────────── */
typedef struct {
    int16_t  recent_values[60];  /* 最近 60 次采样 (5分钟 @ 1s) */
    uint8_t  head;               /* 环形缓冲区头 */
    uint8_t  count;              /* 有效样本数 */
    int16_t  slope;              /* 趋势斜率 (每 tick) */
    uint8_t  slope_strength;     /* 趋势强度 0-100 */
} edge_trend_t;

/* ── 预测器状态 ────────────────────────────────────────── */
typedef struct {
    edge_alert_t   last_alert;
    uint32_t       alert_count;
    uint32_t       total_ticks;
    uint8_t        false_positive_rate;  /* 误报率 0-100 */
    uint8_t        sensitivity;          /* 敏感度 0-100 (默认 70) */
} edge_predictor_state_t;

/* ── 公共 API ──────────────────────────────────────────── */

/* 初始化预测器 */
void edge_predictor_init(void);

/* 每次 tick 调用: 输入传感器模式 → 输出预警
 * 返回: 预警等级 (0=无预警) */
edge_warn_level_t edge_predictor_tick(const edge_pattern_t *pattern,
                                       edge_alert_t *alert_out);

/* 确认预警 (标记为已处理, 降低误报率) */
void edge_predictor_acknowledge(void);

/* 调整敏感度 */
void edge_predictor_set_sensitivity(uint8_t sens);  /* 0-100 */

/* 获取预测器统计 */
const edge_predictor_state_t *edge_predictor_get_state(void);

/* 趋势快照 */
void edge_predictor_trend_snapshot(edge_sensor_type_t type,
                                   int16_t *slope_out,
                                   uint8_t *strength_out);

#ifdef __cplusplus
}
#endif

#endif /* EDGE_PREDICTOR_H */
