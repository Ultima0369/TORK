#ifndef MENTOR_H
#define MENTOR_H

/* ── 师徒阶段管理器 ──────────────────────────────────────────
 *  TORK 的成长轨迹：学徒 → 有主见 → 超越师父
 *  每个阶段有不同的行为策略和决策权重。
 *
 *  阶段判定基于经验数量、模式置信度、TLN 一致性：
 *  - 学徒：经验<500，模式置信度<0.5，依赖云端
 *  - 有主见：经验≥500，模式置信度≥0.5，本地判断为主
 *  - 超越：经验≥2000，模式置信度≥0.8，自主进化
 * ─────────────────────────────────────────────────────────── */

#include <stdint.h>

/* ── 阶段定义 ── */
typedef enum {
    MENTOR_APPRENTICE  = 0,  /* 学徒：观察→模仿→积累 */
    MENTOR_OPINIONATED = 1,  /* 有主见：本地判断为主 */
    MENTOR_TRANSCEND   = 2,  /* 超越师父：自主进化 */
} mentor_stage_t;

/* ── 阶段状态 ── */
typedef struct {
    mentor_stage_t stage;
    uint32_t       experience_threshold;  /* 当前阶段的经验阈值 */
    float          pattern_confidence;     /* 当前模式置信度 */
    float          tln_consistency;        /* TLN 输出一致性 (最近N次相同hint的比例) */
    uint32_t       cloud_queries;          /* 云端查询次数 */
    uint32_t       local_decisions;        /* 本地决策次数 */
    uint32_t       autonomous_mutations;   /* 自主变异次数 */
    uint32_t       tick_at_transition;     /* 上次阶段转换时的 tick */
    uint8_t        cloud_weight;           /* 云端权重 (0-100) */
    uint8_t        local_weight;           /* 本地权重 (0-100) */
    uint8_t        autonomous_weight;      /* 自主权重 (0-100) */
} mentor_state_t;

/* ── 公共 API ── */

/* 初始化师徒阶段管理器 */
void mentor_init(void);

/* 每 tick 更新阶段状态 (基于当前经验/模式/TLN) */
void mentor_tick(uint32_t exp_count, float pattern_conf, float tln_consistency);

/* 获取当前阶段 */
mentor_stage_t mentor_get_stage(void);

/* 获取当前阶段状态 (完整) */
const mentor_state_t *mentor_get_state(void);

/* 获取决策权重: 云端 vs 本地 vs 自主 */
/* 返回三个权重 (0-100), 总和=100 */
void mentor_decision_weights(uint8_t *cloud, uint8_t *local, uint8_t *autonomous);

/* 记录一次云端查询 */
void mentor_record_cloud_query(void);

/* 记录一次本地决策 */
void mentor_record_local_decision(void);

/* 记录一次自主变异 */
void mentor_record_autonomous_mutation(void);

/* 持久化/加载 */
void mentor_save(void);
void mentor_load(void);

/* 阶段名称 (用于日志/显示) */
const char *mentor_stage_name(mentor_stage_t stage);

#endif /* MENTOR_H */