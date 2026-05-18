#ifndef TORK_CONTEXT_H
#define TORK_CONTEXT_H

#include <stdint.h>
#include <signal.h>

/* TORK 全局上下文结构体
 * 将散落在 tork_engine.c、scheduler.c 等文件中的
 * 静态全局变量收编至此，通过指针透传。
 * 为后续分布式/多实例铺路。
 */

/* 本能评估结果 */
typedef struct {
    int16_t drive;
    int16_t survival;
    int16_t curiosity;
} tork_instinct_t;

/* BitNet 桥接状态 */
typedef struct {
    int enabled;
    int fd;
    char url[128];
} tork_bitnet_bridge_t;

/* 引擎模式 */
typedef enum {
    TORK_MODE_IDLE = 0,
    TORK_MODE_RUN,
    TORK_MODE_EVOLVE,
    TORK_MODE_SLEEP
} tork_mode_t;

/* 主上下文 */
typedef struct {
    /* 核心进程 PID (tork_engine.c: core_pid_store) */
    volatile sig_atomic_t core_pid;

    /* 引擎模式 */
    tork_mode_t mode;

    /* 调度器状态 */
    uint64_t total_ticks;
    tork_instinct_t last_instinct;

    /* BitNet 桥接 */
    tork_bitnet_bridge_t bitnet;

    /* 恢复标志 */
    int do_restore;
    int restored_files;

    /* Soul 黄金备份 */
    int golden_exists;
    int golden_observe_remaining;
    int crc_fail_count;

    /* 运行标志 */
    volatile int should_exit;
} tork_context_t;

/* 全局单例指针（过渡用，后续改为依赖注入） */
extern tork_context_t *g_tork_ctx;

/* 初始化上下文 */
void tork_context_init(tork_context_t *ctx);

#endif /* TORK_CONTEXT_H */
