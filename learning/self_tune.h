#ifndef SELF_TUNE_H
#define SELF_TUNE_H

#include <stdint.h>

/* ── TORK 自主参数调优 ──────────────────────────────────────
 *  把模式学习的输出映射到长期参数调整。
 *  不需要云端，不需要人类——TORK 自己调自己。
 * ──────────────────────────────────────────────────────────── */

/* 可调参数集 */
typedef struct {
    float fear_weight;         /* 恐惧权重 (default 1.0) */
    float desire_weight;       /* 欲望权重 (default 1.0) */
    float curiosity_weight;    /* 好奇心权重 (default 1.0) */
    float learning_rate;       /* 学习速率 (default 0.1) */
    int   heartbeat_interval;  /* 心跳间隔 (ms, default 500) */
    int   exploration_rate;    /* 探索率 % (default 20) */
} tune_params_t;

/* 初始化调优器 */
void tune_init(float fear_base, float desire_base, float curiosity_base);

/* 根据模式统计调整参数。在每次模式学习后调用 */
void tune_adjust_from_patterns(void);

/* 获取当前调优参数 */
tune_params_t tune_get_params(void);

/* 保存/加载调优参数 */
int tune_save(void);
int tune_load(void);

/* 打印当前参数 */
void tune_print(void);

#endif /* SELF_TUNE_H */
