#ifndef SOUL_SIZE
#define SOUL_SIZE 192
#endif
#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include <stdint.h>

/* ── TORK 快照/提交协议 ──────────────────────────────────────
 *  自动保存 Soul 的健康快照, 检测退化, 回滚到最后一个健康状态。
 *  这是 TORK 的「自愈」能力——不是免疫伤害, 是能从伤害中恢复。
 * ──────────────────────────────────────────────────────────── */

#define SNAP_MAX_HISTORY  8    /* 保留最近 8 个快照 */
#define SNAP_AUTO_INTERVAL 50  /* 每 50 tick 自动快照一次 */
#define SNAP_MIN_TICKS_BETWEEN 10  /* 两次快照的最小间隔 */

/* ── 单次快照 ──────────────────────────────────────────────── */
typedef struct {
    uint64_t tick;              /* 快照时的 tick */
    int64_t  drive;             /* 当时 drive */
    uint8_t  hw_stress;         /* 当时 stress */
    uint64_t gen_count;         /* 当时世代 */
    
    /* Soul 完整内容 */
    uint8_t  soul_data[SOUL_SIZE];    /* 完整 Soul v3.0 快照 */
    
    uint32_t checksum;          /* 快照自身的 CRC */
} snapshot_t;

/* ── 快照历史 ──────────────────────────────────────────────── */
typedef struct {
    snapshot_t slots[SNAP_MAX_HISTORY];
    uint32_t   head;            /* 最新快照索引 */
    uint32_t   count;           /* 总快照数 */
    uint32_t   last_restore_tick; /* 上次回滚的 tick */
    uint32_t   restores;        /* 总回滚次数 */
    uint32_t   commits;         /* 总提交次数 */
    uint32_t   last_commit_tick;  /* 上次提交的tick */
    uint32_t   commit_interval;   /* 当前提交间隔, 动态调整 */
} snapshot_history_t;

/* ── 退化检测结果 ───────────────────────────────────────────── */
typedef struct {
    int      degraded;          /* 1=检测到退化 */
    float    drive_drop;        /* drive 下降幅度 */
    int      stress_spike;      /* stress 是否飙升 */
    int      soul_crc_fail;     /* Soul CRC 是否失败 */
    uint32_t best_snapshot_idx; /* 最佳回滚目标索引 */
} health_check_t;

/* ── Public API ────────────────────────────────────────────── */

/* 初始化 */
void snap_init(void);

/* 自动快照：如果条件满足, 保存当前状态快照 */
void snap_auto(uint64_t tick, int64_t drive, uint8_t hw_stress,
               uint64_t gen_count, const uint8_t *soul_data);

/* 手动强制快照 */
void snap_force(uint64_t tick, int64_t drive, uint8_t hw_stress,
                uint64_t gen_count, const uint8_t *soul_data);

/* 健康检查：比较当前状态与历史快照, 判断是否退化 */
health_check_t snap_health_check(uint64_t tick, int64_t drive, 
                                  uint8_t hw_stress, int soul_crc_ok);

/* 回滚到最优快照：返回要恢复的快照数据 */
/* 返回: 写入 soul_data 的快照数 (0=无可用快照) */
int snap_rollback(uint8_t *soul_data, uint32_t *restore_tick);

/* 获取上次回滚的 tick */
uint32_t snap_last_restore(void);

/* 获取回滚次数 */
uint32_t snap_restore_count(void);

/* 保存快照历史到磁盘 */
int snap_save(void);

/* 从磁盘加载快照历史 */
int snap_load(void);

/* 提交当前快照为"已确认健康"状态 */
/* 这样未来的回滚会优先回到已提交的状态 */
void snap_commit(uint64_t tick, int64_t drive, uint8_t hw_stress,
                 uint64_t gen_count, const uint8_t *soul_data);

/* 获取已提交的快照数 */
uint32_t snap_committed_count(void);

/* 打印快照状态 */
void snap_commit(uint64_t tick, int64_t drive, uint8_t hw_stress,
                 uint64_t gen_count, const uint8_t *soul_data);
uint32_t snap_committed_count(void);
void snap_print_status(void);

#endif /* SNAPSHOT_H */
