#ifndef BRANCH_H
#define BRANCH_H

#include <stdint.h>
#include "../engine/soul_access.h"

/* ── TORK 灵魂分岔引擎 ───────────────────────────────────────
 *  让主干在不停止循环的前提下，生成短暂生命的变体分支。
 *  分支继承主干灵魂状态独立运行，死亡时将一生经验回报主干。
 *  就像树在春天试探着发出新枝。
 * ──────────────────────────────────────────────────────────── */

/* 分支生命周期（圈数） */
#define BRANCH_DEFAULT_LIFETIME  1000

/* 活跃分支上限 */
#define BRANCH_MAX_ACTIVE        8

/* 分岔冷却间隔（tick） */
#define BRANCH_COOLDOWN_TICKS    1000

/* 死因编码 */
#define DEATH_NONE               0x00  /* 存活中 / 未死亡 */
#define DEATH_TIMEOUT            0x01  /* 大限已到，自然凋零 */
#define DEATH_SOUL_CORRUPTION    0x02  /* CRC 校验失败 */
#define DEATH_SANDBOX_VIOLATION  0x03  /* 沙箱违规 */
#define DEATH_DRIVE_COLLAPSE     0x04  /* 驱动值崩溃（负向峰值） */
#define DEATH_EXEC_FAILURE       0x05  /* 执行上下文故障 */

/* ── 分支执行上下文 ───────────────────────────────────────── */
typedef struct {
    uint8_t  active;             /* 1=活跃, 0=空闲槽位 */
    soul_t   soul;               /* 分支独立的 Soul（通过 /proc/PID/mem 读写模拟） */
    uint64_t ticks_lived;        /* 已存活的 tick 数 */
    uint64_t reg_snapshot[8];    /* 寄存器快照（保留） */
    uint32_t branch_id;          /* 分支 ID（冗余，方便调试） */
    uint32_t parent_id;          /* 父分支 ID */
} branch_context_t;

/* ── 分岔请求 ─────────────────────────────────────────────── */
typedef struct {
    int      should_fork;        /* 1=发起分岔 */
    uint32_t current_tick;       /* 当前主干 tick */
    uint16_t gen_count;          /* 当前世代 */
    int8_t   drive;              /* 当前驱动值 */
    uint8_t  sandbox_level;      /* 沙箱级别 */
    uint32_t branch_cool_tick;   /* 上次分岔的 tick */
} fork_request_t;

/* ── 凋零报告 ─────────────────────────────────────────────── */
typedef struct {
    uint32_t branch_id;
    uint32_t parent_id;
    uint64_t death_reason;
    uint64_t ticks_lived;
    int16_t  final_drive;
    int16_t  drive_peak;
    int16_t  drive_end;
    uint8_t  promising;          /* 1=值得将来重新试探 */
} reap_report_t;

/* ── Public API ────────────────────────────────────────────── */

/* 初始化分支引擎 */
void br_init(void);

/* 检查是否可以分岔（由 instinct 层调用） */
int br_should_fork(const fork_request_t *req, float rhythm_dissonance);

/* 创建分支：从主干 soul 分岔出一个子分支 */
int br_fork(soul_t *parent_soul, uint32_t current_tick, uint32_t gen_count);

/* 推进所有活跃分支一步（由主干循环每圈末尾调用） */
void br_advance_all(void);

/* 收割一个分支：记录死亡经验，释放资源 */
reap_report_t br_reap(int branch_index, uint64_t death_reason);

/* 尝试将分支的经验合并回主干（非破坏性） */
void br_merge_if_worthy(int branch_index, soul_t *parent_soul);

/* 获取活跃分支数量 */
int br_active_count(void);

/* 获取上次分岔的 tick */
uint32_t br_last_fork_tick(void);

/* 获取最近一次凋零报告 */
reap_report_t br_last_reap(void);

/* 清理所有分支 */
void br_cleanup(void);

#endif /* BRANCH_H */
