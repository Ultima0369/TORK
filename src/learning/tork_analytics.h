/**
 * tork_analytics.h — TORK 第三课：看数据
 * 日志分析 + 模式识别 + 异常检测
 *
 * TORK 必须能看懂数据，才能从数据里找饭吃。
 * 这个模块给 TORK 轻量级的时间序列分析能力。
 */

#ifndef TORK_ANALYTICS_H
#define TORK_ANALYTICS_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 滑动窗口统计 ====================

#define TORK_ANALYTICS_MAX_WINDOW 256

typedef struct {
    double values[TORK_ANALYTICS_MAX_WINDOW];
    int    count;          // 当前已采集数
    int    capacity;       // 窗口大小
    int    head;           // 环形缓冲区头
    double sum;            // 和
    double sum2;           // 平方和（用于标准差）
} tork_moving_window_t;

/**
 * 初始化滑动窗口
 * capacity: 窗口大小 (1-256)
 */
void tork_moving_window_init(tork_moving_window_t *w, int capacity);

/**
 * 压入新值
 */
void tork_moving_window_push(tork_moving_window_t *w, double value);

/**
 * 均值
 */
double tork_moving_window_mean(const tork_moving_window_t *w);

/**
 * 标准差
 */
double tork_moving_window_stddev(const tork_moving_window_t *w);

/**
 * 最新值（最近压入的）
 */
double tork_moving_window_latest(const tork_moving_window_t *w);

// ==================== 异常检测 ====================

typedef enum {
    ANOMALY_NONE = 0,
    ANOMALY_SPIKE,       // 突然飙升
    ANOMALY_DROP,        // 突然下降
    ANOMALY_DRIFT,       // 持续偏离基线
    ANOMALY_FLUCTUATE,   // 异常波动
    ANOMALY_STALE,       // 长时间无变化（信号丢失）
} tork_anomaly_type_t;

typedef struct {
    tork_anomaly_type_t type;
    double              severity;     // 0.0 - 1.0
    double              current_val;
    double              baseline_mean;
    double              baseline_std;
    char                description[128];
    time_t              timestamp;
} tork_anomaly_t;

/**
 * 检测异常：基于滑动窗口的历史数据，判断最新值是否异常
 * 
 * 规则：
 *   > 3σ  → SPIKE/DROP
 *   > 1σ 持续5次 → DRIFT
 *   方差翻倍 → FLUCTUATE
 *   连续3次无变化 → STALE
 *
 * 返回检测到的异常类型及严重程度
 */
tork_anomaly_t tork_anomaly_detect(const tork_moving_window_t *w);

// ==================== 简单线性回归（趋势分析） ====================

typedef struct {
    double slope;       // 斜率（正=上升，负=下降）
    double intercept;   // 截距
    double r_squared;   // 拟合优度 0-1
    int    n;           // 数据点数量
} tork_trend_t;

/**
 * 对滑动窗口数据进行线性回归
 * 返回趋势：slope > 0.01 → 上升趋势, slope < -0.01 → 下降趋势
 */
tork_trend_t tork_trend_analyze(const tork_moving_window_t *w);

// ==================== 相似度匹配 ====================

/**
 * 计算两个双精度数组的余弦相似度 (-1 到 1)
 * 用于 TORK 识别"这个模式我见过"
 */
double tork_similarity_cosine(const double *a, const double *b, int n);

/**
 * 欧氏距离
 */
double tork_similarity_euclidean(const double *a, const double *b, int n);

// ==================== 日志模式提炼 ====================

/**
 * 从日志行中提取关键模式
 * 将日志中的数字替换为占位符 {n}
 * 如 "CPU temp 67C fan 3200rpm" → "CPU temp {n}C fan {n}rpm"
 * 
 * 返回模式字符串，写入 pattern_buf
 */
void tork_pattern_extract(const char *log_line, char *pattern_buf, int bufsize);

/**
 * 统计某个模式在 last_n 条日志中出现的次数
 * 用于 TORK 识别"这个问题反复出现"
 */
int tork_pattern_count(const char **log_history, int history_count,
                       const char *pattern);

// ==================== 快速傅里叶情感分析（轻量版） ====================

/**
 * 简单的情感倾向分析
 * 统计正负词汇比例
 * 返回值: -1.0 (非常负面) 到 1.0 (非常正面)
 */
double tork_sentiment_analyze(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* TORK_ANALYTICS_H */
