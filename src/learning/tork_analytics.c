/**
 * tork_analytics.c — TORK 看数据实现
 *
 * 纯 C 数学运算，零外部依赖。
 * 所有统计都在滑动窗口内完成，内存占用 ~2KB。
 */

#include "tork_analytics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ==================== 滑动窗口 ====================

void tork_moving_window_init(tork_moving_window_t *w, int capacity) {
    if (!w) return;
    if (capacity < 1) capacity = 1;
    if (capacity > TORK_ANALYTICS_MAX_WINDOW) capacity = TORK_ANALYTICS_MAX_WINDOW;
    memset(w, 0, sizeof(*w));
    w->capacity = capacity;
    w->head = 0;
}

void tork_moving_window_push(tork_moving_window_t *w, double value) {
    if (!w) return;
    if (w->count < w->capacity) {
        w->values[w->count] = value;
        w->sum += value;
        w->sum2 += value * value;
        w->count++;
    } else {
        double old = w->values[w->head];
        w->values[w->head] = value;
        w->sum = w->sum - old + value;
        w->sum2 = w->sum2 - old * old + value * value;
        w->head = (w->head + 1) % w->capacity;
    }
}

double tork_moving_window_mean(const tork_moving_window_t *w) {
    if (!w || w->count == 0) return 0.0;
    return w->sum / w->count;
}

double tork_moving_window_stddev(const tork_moving_window_t *w) {
    if (!w || w->count < 2) return 0.0;
    double mean = w->sum / w->count;
    double variance = (w->sum2 / w->count) - (mean * mean);
    if (variance < 0) variance = 0;
    return sqrt(variance);
}

double tork_moving_window_latest(const tork_moving_window_t *w) {
    if (!w || w->count == 0) return 0.0;
    if (w->count < w->capacity) return w->values[w->count - 1];
    int last = (w->head - 1 + w->capacity) % w->capacity;
    return w->values[last];
}

// ==================== 异常检测 ====================

tork_anomaly_t tork_anomaly_detect(const tork_moving_window_t *w) {
    tork_anomaly_t a;
    memset(&a, 0, sizeof(a));
    a.timestamp = time(NULL);

    if (!w || w->count < 3) {
        a.type = ANOMALY_NONE;
        snprintf(a.description, sizeof(a.description), "insufficient data");
        return a;
    }

    double mean = tork_moving_window_mean(w);
    double std  = tork_moving_window_stddev(w);
    double latest = tork_moving_window_latest(w);

    a.current_val = latest;
    a.baseline_mean = mean;
    a.baseline_std = std;

    if (std < 1e-10) {
        // 几乎无波动
        if (w->count >= 5) {
            // 检查最近5个值是否完全一样
            int all_same = 1;
            int last = (w->head - 1 + w->capacity) % w->capacity;
            double ref = w->values[last];
            for (int i = 0; i < 5 && all_same; i++) {
                int idx = (last - i + w->capacity) % w->capacity;
                if (w->values[idx] != ref) all_same = 0;
            }
            if (all_same) {
                a.type = ANOMALY_STALE;
                a.severity = 0.5;
                snprintf(a.description, sizeof(a.description),
                         "Stale signal: %.2f unchanged for %d samples", latest, 5);
                return a;
            }
        }
        a.type = ANOMALY_NONE;
        snprintf(a.description, sizeof(a.description), "stable (std≈0)");
        return a;
    }

    double z = (latest - mean) / std;
    if (z > 3.0) {
        a.type = ANOMALY_SPIKE;
        a.severity = fmin(1.0, (z - 3.0) / 5.0);
        snprintf(a.description, sizeof(a.description),
                 "SPIKE: %.2f vs baseline %.2f±%.2f (z=%.1fσ)", latest, mean, std, z);
    } else if (z < -3.0) {
        a.type = ANOMALY_DROP;
        a.severity = fmin(1.0, (-z - 3.0) / 5.0);
        snprintf(a.description, sizeof(a.description),
                 "DROP: %.2f vs baseline %.2f±%.2f (z=%.1fσ)", latest, mean, std, -z);
    } else if (fabs(z) > 1.0 && w->count >= 8) {
        // 检查最近3个值是否都偏离基线
        int drifted = 1;
        int last = (w->head - 1 + w->capacity) % w->capacity;
        for (int i = 0; i < 3 && drifted; i++) {
            int idx = (last - i + w->capacity) % w->capacity;
            double vz = (w->values[idx] - mean) / std;
            if (fabs(vz) < 1.0) drifted = 0;
        }
        if (drifted) {
            a.type = ANOMALY_DRIFT;
            a.severity = fmin(1.0, fabs(z) / 5.0);
            snprintf(a.description, sizeof(a.description),
                     "DRIFT: sustained deviation %.1fσ over 3 samples", fabs(z));
            return a;
        }
    }

    // 波动异常：方差最近是否翻倍
    if (w->count >= 10) {
        int half = w->count / 2;
        double sum_half1 = 0, sum_half2 = 0;
        int last = (w->head - 1 + w->capacity) % w->capacity;
        for (int i = 0; i < half; i++) {
            int idx = (last - i + w->capacity) % w->capacity;
            if (i < half / 2) sum_half1 += w->values[idx];
            else sum_half2 += w->values[idx];
        }
        double m1 = sum_half1 / (half / 2);
        double m2 = sum_half2 / (half / 2);
        if (fabs(m2 - mean) > 2 * std && fabs(m1 - mean) < std) {
            a.type = ANOMALY_FLUCTUATE;
            a.severity = 0.4;
            snprintf(a.description, sizeof(a.description),
                     "FLUCTUATE: recent mean %.2f vs baseline %.2f", m2, mean);
            return a;
        }
    }

    a.type = ANOMALY_NONE;
    snprintf(a.description, sizeof(a.description), "normal (z=%.2fσ)", z);
    return a;
}

// ==================== 趋势分析 ====================

tork_trend_t tork_trend_analyze(const tork_moving_window_t *w) {
    tork_trend_t t;
    memset(&t, 0, sizeof(t));
    if (!w || w->count < 3) return t;

    t.n = w->count;
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;

    // 使用索引作为 x 轴
    int last = (w->head - 1 + w->capacity) % w->capacity;
    for (int i = 0; i < w->count; i++) {
        int idx = (last - i + w->capacity) % w->capacity;
        double x = (double)i;
        double y = w->values[idx];
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_x2 += x * x;
    }

    double n = (double)w->count;
    double denom = n * sum_x2 - sum_x * sum_x;
    if (fabs(denom) < 1e-15) return t;

    t.slope = (n * sum_xy - sum_x * sum_y) / denom;
    t.intercept = (sum_y - t.slope * sum_x) / n;

    // R²
    double mean_y = sum_y / n;
    double ss_res = 0, ss_tot = 0;
    for (int i = 0; i < w->count; i++) {
        int idx = (last - i + w->capacity) % w->capacity;
        double x = (double)i;
        double y = w->values[idx];
        double y_pred = t.slope * x + t.intercept;
        ss_res += (y - y_pred) * (y - y_pred);
        ss_tot += (y - mean_y) * (y - mean_y);
    }
    t.r_squared = (ss_tot > 1e-15) ? 1.0 - ss_res / ss_tot : 0.0;

    return t;
}

// ==================== 相似度 ====================

double tork_similarity_cosine(const double *a, const double *b, int n) {
    if (!a || !b || n <= 0) return 0.0;
    double dot = 0, na = 0, nb = 0;
    for (int i = 0; i < n; i++) {
        dot += a[i] * b[i];
        na  += a[i] * a[i];
        nb  += b[i] * b[i];
    }
    double denom = sqrt(na) * sqrt(nb);
    return (denom > 1e-15) ? dot / denom : 0.0;
}

double tork_similarity_euclidean(const double *a, const double *b, int n) {
    if (!a || !b || n <= 0) return 0.0;
    double sum = 0;
    for (int i = 0; i < n; i++) {
        double d = a[i] - b[i];
        sum += d * d;
    }
    return sqrt(sum);
}

// ==================== 模式提取 ====================

void tork_pattern_extract(const char *log_line, char *pattern_buf, int bufsize) {
    if (!log_line || !pattern_buf || bufsize <= 0) return;
    int pi = 0;
    for (int i = 0; log_line[i] && pi < bufsize - 1; i++) {
        char c = log_line[i];
        if (c >= '0' && c <= '9') {
            // 跳过连续数字
            pattern_buf[pi++] = '{';
            pattern_buf[pi++] = 'n';
            pattern_buf[pi++] = '}';
            while (log_line[i] >= '0' && log_line[i] <= '9') i++;
            if (!log_line[i]) break;
            i--; // for 循环会再++
        } else {
            pattern_buf[pi++] = c;
        }
    }
    pattern_buf[pi] = 0;
}

int tork_pattern_count(const char **log_history, int history_count,
                       const char *pattern) {
    if (!log_history || !pattern || history_count <= 0) return 0;
    int count = 0;
    for (int i = 0; i < history_count; i++) {
        if (log_history[i] && strstr(log_history[i], pattern))
            count++;
    }
    return count;
}

// ==================== 情感分析（轻量） ====================

static const char *pos_words[] = {
    "good", "great", "excellent", "success", "pass", "ok", "stable",
    "healthy", "normal", "clean", "ready", "done", "completed",
    "yes", "true", "positive", "happy", "nice", "fine", "perfect",
    NULL
};

static const char *neg_words[] = {
    "fail", "error", "bad", "critical", "dead", "panic", "crash",
    "broken", "corrupt", "invalid", "missing", "refused", "timeout",
    "no", "false", "negative", "wrong", "damage", "emergency",
    NULL
};

double tork_sentiment_analyze(const char *text) {
    if (!text || !text[0]) return 0.0;

    // 转小写
    char lower[1024];
    int len = 0;
    for (int i = 0; text[i] && len < 1023; i++) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') lower[len++] = c + 32;
        else lower[len++] = c;
    }
    lower[len] = 0;

    int pos_count = 0, neg_count = 0;

    for (int i = 0; pos_words[i]; i++) {
        if (strstr(lower, pos_words[i])) pos_count++;
    }
    for (int i = 0; neg_words[i]; i++) {
        if (strstr(lower, neg_words[i])) neg_count++;
    }

    int total = pos_count + neg_count;
    if (total == 0) return 0.0;
    return (double)(pos_count - neg_count) / (double)total;
}
