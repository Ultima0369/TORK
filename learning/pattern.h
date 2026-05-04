#ifndef PATTERN_H
#define PATTERN_H

#include <stdint.h>

/* ── TORK 模式学习引擎 ───────────────────────────────────────
 *  从自身经验中识别规律：给定当前状态 (stress, drive, gen)，
 *  从历史中找出相似情境下的最优行动。
 *  这就是「闲时思过去」的工程化——把零散经验压缩成可重用的规律。
 * ──────────────────────────────────────────────────────────── */

#define PATTERN_MAX_SLOTS  64   /* 最多记住 64 种情境模式 */
#define PATTERN_MIN_SAMPLES 3   /* 最少 3 条经验才能形成模式 */

/* ── 情境签名：压缩状态到模式键 ───────────────────────────────
 *  把连续状态空间离散化为有限的「情境桶」。
 *  例如 hw_stress 0-3 + drive 分 8 档 + gen_count 分 4 档 = 128 种可能情境。
 *  但我们只存储实际遇到过的、且有统计意义的那些。
 * ──────────────────────────────────────────────────────────── */
typedef struct {
    uint8_t  hw_stress;          /* 0-3 */
    int8_t   drive_bucket;       /* drive 分档: -4,-3,-2,-1,0,1,2,3 (量化) */
    uint8_t  gen_bucket;         /* 世代分档: 0-3 (for now) */
    uint8_t  action_type;        /* 哪种行动 */
} pattern_key_t;

/* ── 模式统计 ──────────────────────────────────────────────── */
typedef struct {
    pattern_key_t key;           /* 情境标识 */
    int32_t  total_outcome;      /* 该情境下所有 outcome 之和 */
    int32_t  total_crashes;      /* 崩溃次数 */
    int32_t  sample_count;       /* 样本数 */
    float    avg_outcome;        /* 平均 outcome */
    float    crash_rate;         /* 崩溃率 */
    uint32_t last_seen_tick;     /* 上次匹配到该模式的 tick */
    uint8_t  active;             /* 1=有效模式 */
} pattern_t;

/* ── 模式学习器 ────────────────────────────────────────────── */
typedef struct {
    pattern_t slots[PATTERN_MAX_SLOTS];
    uint32_t  total_patterns;    /* 已学习的模式数 */
    uint32_t  learn_cycles;      /* 学习轮数 */
} pattern_learner_t;

/* ── Public API ────────────────────────────────────────────── */

/* 初始化模式学习器 */
void pat_init(void);

/* 从经验缓冲区学习：扫描最近 N 条经验，提取模式 */
void pat_learn_from_experience(void);

/* 查询最佳行动：在当前状态下，从模式库中推荐最优行动 */
/* 返回值: action_type (0-6), 通过 confidence 返回置信度 */
int pat_query_best_action(uint8_t hw_stress, int8_t drive, 
                          uint16_t gen_count, float *confidence);

/* 查询某个特定行动在该情境下的期望 outcome */
float pat_predict_outcome(uint8_t hw_stress, int8_t drive,
                          uint16_t gen_count, uint8_t action_type);

/* 获取已学习的模式数量 */
int pat_count(void);

/* 获取学习轮数 */
uint32_t pat_cycles(void);

/* 清理 */
void pat_cleanup(void);

#endif /* PATTERN_H */
