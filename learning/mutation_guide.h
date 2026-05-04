#ifndef MUTATION_GUIDE_H
#define MUTATION_GUIDE_H

#include <stdint.h>

/* ── 经验驱动的变异引导引擎 ────────────────────────────────
 *  连接模式学习系统与进化引擎:
 *  从历史经验中发现"什么类型的变异容易成功",
 *  然后用这些知识引导下一次变异的方向。
 * ──────────────────────────────────────────────────────── */

#define MG_MAX_STRATEGIES  32  /* 最多32种变异策略 */
#define MG_HISTORY_SIZE    64  /* 保留最近64次变异记录 */

/* ── 变异策略类型 ──────────────────────────────────────────── */
typedef enum {
    MG_STRATEGY_RANDOM = 0,        /* 纯随机 */
    MG_STRATEGY_INSTINCT,          /* 修改本能参数 */
    MG_STRATEGY_PATTERN_THRESHOLD, /* 调整模式识别阈值 */
    MG_STRATEGY_LEARNING_RATE,     /* 调整学习率 */
    MG_STRATEGY_CURIOSITY,         /* 调整好奇心衰减 */
    MG_STRATEGY_BRANCH_LIFETIME,   /* 调整分支寿命 */
    MG_STRATEGY_ENERGY_PARAMS,     /* 调整能量参数 */
    MG_STRATEGY_WATCHER_FOCUS,     /* 调整观察焦点 */
} mg_strategy_type_t;

/* ── 变异结果记录 ──────────────────────────────────────────── */
typedef struct {
    uint32_t            id;
    mg_strategy_type_t  strategy;
    int                 gen;
    int                 success;        /* 1=成功, 0=失败 */
    float               fitness_before;
    float               fitness_after;
    uint32_t            tick;
    char                description[64];
} mg_record_t;

/* ── 策略权重 ──────────────────────────────────────────────── */
typedef struct {
    mg_strategy_type_t  strategy;
    float               weight;         /* 0-1, 越高越优先 */
    uint32_t            attempts;
    uint32_t            successes;
    float               success_rate;
    char                name[32];
} mg_strategy_t;

/* ── 引导引擎状态 ──────────────────────────────────────────── */
typedef struct {
    mg_strategy_t       strategies[MG_MAX_STRATEGIES];
    uint32_t            strategy_count;
    mg_record_t         history[MG_HISTORY_SIZE];
    uint32_t            history_head;
    uint32_t            total_attempts;
    uint32_t            total_successes;
} mutation_guide_t;

/* ── API ─────────────────────────────────────────────────── */

/* 初始化引导引擎 */
void mg_init(void);

/* 根据历史经验, 推荐本次变异策略 */
/* 返回策略类型, 填写recommendation描述 */
mg_strategy_type_t mg_recommend(char *recommendation, int buf_size);

/* 记录一次变异结果 */
void mg_record_result(mg_strategy_type_t strategy, int success,
                      float fitness_before, float fitness_after,
                      const char *description);

/* 获取当前成功率最高的策略 */
mg_strategy_type_t mg_best_strategy(void);

/* 获取摘要 */
void mg_summary(char *buf, int buf_size);

/* 保存/加载 */
int mg_save(void);
int mg_load(void);

#endif /* MUTATION_GUIDE_H */
