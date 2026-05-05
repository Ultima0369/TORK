#ifndef REPLAY_H
#define REPLAY_H

#include <stdint.h>

/* ── TORK 深度回放引擎 ───────────────────────────────────────
 *  闲时在脑子里重放过去的经验，模拟"如果当时选了另一条路会怎样"。
 *  这是人类「做梦」的工程对应——不是无意义的回放，而是关联、重组、发现。
 * ──────────────────────────────────────────────────────────── */

#define REPLAY_BATCH_SIZE  16   /* 每次回放批处理的经验数 */
#define REPLAY_SIM_DEPTH   8    /* 每次模拟的替代路径深度 */

/* ── 回放结果 ──────────────────────────────────────────────── */
typedef struct {
    uint32_t experiences_played;   /* 回放了多少条旧经验 */
    uint32_t new_insights;         /* 发现了多少新模式 */
    uint32_t alternative_count;    /* 发现了多少替代路径 */
    
    /* 最佳发现 */
    int8_t   best_action;          /* 替代路径中发现的最佳行动 */
    float    best_improvement;     /* 相比原路径的改进幅度 */
    uint8_t  best_context_stress;  /* 最佳发现时的 stress 值 */
    int8_t   best_context_drive;   /* 最佳发现时的 drive 档位 */
    
    uint8_t  active;               /* 1=回放完成 */
} replay_result_t;

/* ── Public API ────────────────────────────────────────────── */

/* 执行一次深度回放 */
replay_result_t replay_deep(void);

/* 获取上次回放结果 */
replay_result_t replay_last_result(void);

#endif /* REPLAY_H */
