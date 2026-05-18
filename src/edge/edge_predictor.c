#include "edge_predictor.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ── 内部状态 ──────────────────────────────────────────── */
static edge_predictor_state_t g_state;
static edge_trend_t g_trends[32];  /* 每个传感器一个趋势 */

/* ── 趋势衰减阈值 ──────────────────────────────────────── */
#define TREND_SLOPE_WARN  3   /* 斜率 >3 触发关注 */
#define TREND_SLOPE_CAUTION 6 /* 斜率 >6 触发谨慎 */
#define TREND_SLOPE_EARLY 12  /* 斜率 >12 触发早期预警 */
#define ANOMALY_WARN  1000    /* 异常分数阈值: 关注 */
#define ANOMALY_CAUTION 3000  /* 异常分数阈值: 谨慎 */
#define ANOMALY_EARLY  8000   /* 异常分数阈值: 早期 */
#define ANOMALY_CRITICAL 15000 /* 异常分数阈值: 临界 */
#define LEAD_TIME_FACTOR 20   /* 预估提前量 = 20min / (斜率强度+1) */

static const char *warn_name[] = {
    "NORMAL", "ATTENTION", "CAUTION", "EARLY WARNING", "CRITICAL"
};

/* ── 初始化 ────────────────────────────────────────────── */
void edge_predictor_init(void) {
    memset(&g_state, 0, sizeof(g_state));
    memset(g_trends, 0, sizeof(g_trends));
    g_state.sensitivity = 70;
    printf("  EDGE PREDICTOR: initialized (sensitivity=%d%%)\n", g_state.sensitivity);
}

/* ── 更新趋势 ──────────────────────────────────────────── */
static void update_trend(edge_sensor_type_t type, uint16_t value) {
    if (type <= 0 || type >= 32) return;
    edge_trend_t *t = &g_trends[type];

    /* 环形缓冲区 */
    t->recent_values[t->head] = (int16_t)value;
    t->head = (t->head + 1) % 60;
    if (t->count < 60) t->count++;

    /* 计算斜率: 线性回归简化版 (最近10个点 vs 之前10个点) */
    if (t->count >= 20) {
        int idx_new_start = (t->head - 10 + 60) % 60;
        int idx_old_start = (t->head - 20 + 60) % 60;

        int32_t sum_new = 0, sum_old = 0;
        for (int i = 0; i < 10; i++) {
            sum_new += t->recent_values[(idx_new_start + i) % 60];
            sum_old += t->recent_values[(idx_old_start + i) % 60];
        }

        int16_t diff = (int16_t)((sum_new / 10) - (sum_old / 10));
        t->slope = diff;

        /* 斜率强度: 归一化 */
        int abs_slope = diff < 0 ? -diff : diff;
        if (abs_slope > 50) abs_slope = 50;
        t->slope_strength = (uint8_t)(abs_slope * 2);  /* 0-100 */
    }
}

/* ── 主 tick ───────────────────────────────────────────── */
edge_warn_level_t edge_predictor_tick(const edge_pattern_t *pattern,
                                       edge_alert_t *alert_out) {
    if (!pattern) return WARN_NONE;

    g_state.total_ticks++;
    edge_warn_level_t level = WARN_NONE;
    edge_alert_t alert;
    memset(&alert, 0, sizeof(alert));
    alert.timestamp_ms = pattern->timestamp_ms;
    alert.confidence = pattern->confidence * 10;  /* 0-1000 */
    alert.sensor_count = pattern->num_active;

    /* ── 1. 异常分数检测 ────────────────────────────── */
    uint16_t anomaly = pattern->anomaly_score;
    if (anomaly > ANOMALY_CRITICAL) {
        level = WARN_CRITICAL;
        snprintf(alert.message, sizeof(alert.message),
                 "CRITICAL: anomaly score %u — immediate action required", anomaly);
        alert.lead_time_min = 0;
    } else if (anomaly > ANOMALY_EARLY) {
        level = WARN_EARLY;
        alert.lead_time_min = 5;
        snprintf(alert.message, sizeof(alert.message),
                 "EARLY WARNING: anomaly score %u — trend detected %d min ahead",
                 anomaly, alert.lead_time_min);
    } else if (anomaly > ANOMALY_CAUTION) {
        level = WARN_CAUTION;
        alert.lead_time_min = 10;
        snprintf(alert.message, sizeof(alert.message),
                 "CAUTION: anomaly score %u — monitoring closely", anomaly);
    } else if (anomaly > ANOMALY_WARN) {
        level = WARN_ATTENTION;
        alert.lead_time_min = 15;
        snprintf(alert.message, sizeof(alert.message),
                 "ATTENTION: anomaly score %u — pattern deviation detected", anomaly);
    }

    /* ── 2. 趋势检测 (更敏感) ────────────────────────── */
    /* 检查每个活跃传感器的趋势 */
    for (int i = 0; i < pattern->num_active; i++) {
        /* 更新趋势 (简化: 用 pattern 值模拟传感器索引) */
        int8_t pval = pattern->pattern[i];
        if (pval == 0) continue;  /* 正常值不触发趋势告警 */

        uint16_t mock_val = 1000 + pval * 500;  /* 模拟值 */
        update_trend(i + 1, mock_val);

        edge_trend_t *t = &g_trends[i + 1];
        int abs_slope = t->slope < 0 ? -t->slope : t->slope;

        /* 趋势比异常分数更早发现变化 */
        if (abs_slope >= TREND_SLOPE_EARLY && level < WARN_EARLY) {
            level = WARN_EARLY;
            alert.lead_time_min = LEAD_TIME_FACTOR / (t->slope_strength + 1);
            if (alert.lead_time_min < 3) alert.lead_time_min = 3;
            snprintf(alert.message, sizeof(alert.message),
                     "EARLY WARNING: steep trend (slope=%d) — %d min ahead",
                     t->slope, alert.lead_time_min);
        } else if (abs_slope >= TREND_SLOPE_CAUTION && level < WARN_CAUTION) {
            level = WARN_CAUTION;
            alert.lead_time_min = 5;
        } else if (abs_slope >= TREND_SLOPE_WARN && level < WARN_ATTENTION) {
            level = WARN_ATTENTION;
            alert.lead_time_min = 10;
        }
    }

    /* ── 3. 敏感度调整 ────────────────────────────── */
    if (level > WARN_NONE) {
        /* 敏感度 50 意味着只有 50% 强度以上的预警才输出 */
        if (g_state.sensitivity < 50 && level < WARN_CRITICAL) {
            level = WARN_NONE;  /* 低敏感度模式下忽略非严重预警 */
        }
    }

    /* ── 输出 ────────────────────────────────────────── */
    if (level > WARN_NONE) {
        alert.level = level;
        memcpy(&g_state.last_alert, &alert, sizeof(alert));
        g_state.alert_count++;

        /* 记录涉及的传感器 */
        for (int i = 0; i < pattern->num_active && i < 8; i++) {
            alert.sensors[i] = (edge_sensor_type_t)(i + 1);
        }

        if (alert_out) memcpy(alert_out, &alert, sizeof(alert));

        printf("  EDGE ALERT [%s]: %s (lead=%dmin, conf=%d)\n",
               warn_name[level], alert.message,
               alert.lead_time_min, alert.confidence);
    }

    return level;
}

/* ── 确认预警 ──────────────────────────────────────────── */
void edge_predictor_acknowledge(void) {
    /* 确认后降低误报率计数 */
    if (g_state.false_positive_rate > 0)
        g_state.false_positive_rate--;
    printf("  EDGE PREDICTOR: alert acknowledged\n");
}

/* ── 设置敏感度 ────────────────────────────────────────── */
void edge_predictor_set_sensitivity(uint8_t sens) {
    if (sens > 100) sens = 100;
    g_state.sensitivity = sens;
    printf("  EDGE PREDICTOR: sensitivity set to %d%%\n", sens);
}

/* ── 获取状态 ──────────────────────────────────────────── */
const edge_predictor_state_t *edge_predictor_get_state(void) {
    return &g_state;
}

/* ── 趋势快照 ──────────────────────────────────────────── */
void edge_predictor_trend_snapshot(edge_sensor_type_t type,
                                   int16_t *slope_out,
                                   uint8_t *strength_out) {
    if (type <= 0 || type >= 32) return;
    edge_trend_t *t = &g_trends[type];
    if (slope_out) *slope_out = t->slope;
    if (strength_out) *strength_out = t->slope_strength;
}
