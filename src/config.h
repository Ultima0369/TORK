#ifndef TORK_CONFIG_H
#define TORK_CONFIG_H

/* ══════════════════════════════════════════════════════════════
 * TORK 集中配置中心
 *
 * 所有硬编码参数统一在此处声明。
 * 编译时可通过 -DTORK_CFG_xxx=yyy 覆盖。
 * 不再需要在各模块源文件中搜索魔数。
 * ══════════════════════════════════════════════════════════════ */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── 版本号 ──────────────────────────────────────────────── */
#ifndef TORK_VERSION_MAJOR
#define TORK_VERSION_MAJOR  0
#endif
#ifndef TORK_VERSION_MINOR
#define TORK_VERSION_MINOR  9
#endif
#ifndef TORK_VERSION_PATCH
#define TORK_VERSION_PATCH  0
#endif

/* ── 路径 ────────────────────────────────────────────────── */
#ifndef TORK_PERSIST_DIR
#define TORK_PERSIST_DIR    "persist"
#endif
#ifndef TORK_SOUL_PATH
#define TORK_SOUL_PATH      TORK_PERSIST_DIR "/soul_golden.bin"
#endif
#ifndef TORK_MCTS_PATH
#define TORK_MCTS_PATH      TORK_PERSIST_DIR "/mcts_tree.bin"
#endif
#ifndef TORK_ROLLBACK_DIR
#define TORK_ROLLBACK_DIR   TORK_PERSIST_DIR "/rollback"
#endif
#ifndef TORK_MENTOR_PATH
#define TORK_MENTOR_PATH    TORK_PERSIST_DIR "/mentor_state.bin"
#endif

/* ── Socket ───────────────────────────────────────────────── */
#ifndef TORK_SOCK_PATH
#define TORK_SOCK_PATH      "/tmp/torkd.sock"
#endif
#ifndef TORK_SOCK_BACKLOG
#define TORK_SOCK_BACKLOG   5
#endif
#ifndef TORK_SOCK_TIMEOUT
#define TORK_SOCK_TIMEOUT   30000   /* ms */
#endif
#ifndef TORK_HEARTBEAT_MS
#define TORK_HEARTBEAT_MS   1000    /* 1 second */
#endif

/* ── Soul (灵魂) ─────────────────────────────────────────── */
#ifndef TORK_SOUL_SIZE
#define TORK_SOUL_SIZE      208     /* bytes, 内存映射 0x200000 */
#endif
#ifndef TORK_SOUL_DEFAULT_GEN
#define TORK_SOUL_DEFAULT_GEN  6
#endif
#ifndef TORK_SOUL_DEFAULT_HEARTBEAT
#define TORK_SOUL_DEFAULT_HEARTBEAT 60
#endif

/* ── 分布式网络 ──────────────────────────────────────────── */
#ifndef TORK_MCAST_GROUP
#define TORK_MCAST_GROUP    "239.42.69.42"
#endif
#ifndef TORK_MCAST_PORT
#define TORK_MCAST_PORT     42069
#endif
#ifndef TORK_MCAST_TTL
#define TORK_MCAST_TTL      1       /* 本地子网 */
#endif
#ifndef TORK_DIST_APP_ID
#define TORK_DIST_APP_ID    0x544F524B  /* "TORK" */
#endif
#ifndef TORK_DIST_TOKEN
#define TORK_DIST_TOKEN     0x544B4E47  /* "TKNG" */
#endif
#ifndef TORK_DIST_MAX_PEERS
#define TORK_DIST_MAX_PEERS 64
#endif
#ifndef TORK_DIST_HEARTBEAT_SEC
#define TORK_DIST_HEARTBEAT_SEC  30
#endif
#ifndef TORK_DIST_PRUNE_SEC
#define TORK_DIST_PRUNE_SEC     300 /* 5分钟无信号视为离线 */
#endif
#ifndef TORK_DIST_MAX_MSG
#define TORK_DIST_MAX_MSG   1400    /* < 1500 MTU */
#endif

/* ── 自修改代码 ──────────────────────────────────────────── */
#ifndef TORK_MAX_SNAPSHOTS
#define TORK_MAX_SNAPSHOTS  5
#endif
#ifndef TORK_MODIFY_TIMEOUT
#define TORK_MODIFY_TIMEOUT 30000   /* 30s 编译超时 */
#endif

/* ── 学习系统 ────────────────────────────────────────────── */
#ifndef TORK_MCTS_MAX_NODES
#define TORK_MCTS_MAX_NODES     512
#endif
#ifndef TORK_MCTS_SIMULATIONS
#define TORK_MCTS_SIMULATIONS   128
#endif
#ifndef TORK_MCTS_EXPLORATION
#define TORK_MCTS_EXPLORATION   1.414f
#endif
#ifndef TORK_EXP_BUFFER_SIZE
#define TORK_EXP_BUFFER_SIZE    10000
#endif
#ifndef TORK_TLN_OBSERVE_TIMEOUT
#define TORK_TLN_OBSERVE_TIMEOUT 120
#endif

/* ── 容错 ────────────────────────────────────────────────── */
#ifndef TORK_MAX_CRASH_RATE
#define TORK_MAX_CRASH_RATE     0.3f    /* 30% 崩溃率触发安全模式 */
#endif
#ifndef TORK_AUTO_RECOVER_MAX
#define TORK_AUTO_RECOVER_MAX   5       /* 自动恢复最多尝试版本数 */
#endif

/* ── 师徒系统 ────────────────────────────────────────────── */
#ifndef TORK_APPRENTICE_EXP
#define TORK_APPRENTICE_EXP     500
#endif
#ifndef TORK_OPINIONATED_EXP
#define TORK_OPINIONATED_EXP    2000
#endif
#ifndef TORK_TRANSCEND_CONFIDENCE
#define TORK_TRANSCEND_CONFIDENCE 0.8f
#endif

/* ── 桥接 (SP) ───────────────────────────────────────────── */
#ifndef TORK_SP_HOST
#define TORK_SP_HOST        "127.0.0.1"
#endif
#ifndef TORK_SP_PORT
#define TORK_SP_PORT        9876
#endif
#ifndef TORK_SP_RECONNECT_MS
#define TORK_SP_RECONNECT_MS    5000
#endif
#ifndef TORK_SP_TIMEOUT_MS
#define TORK_SP_TIMEOUT_MS      3000
#endif
#ifndef TORK_SP_HEARTBEAT_INTERVAL
#define TORK_SP_HEARTBEAT_INTERVAL 5000
#endif

/* ── 存档 (Archive) ──────────────────────────────────────── */
#ifndef TORK_ARCHIVE_MAX_VERSIONS
#define TORK_ARCHIVE_MAX_VERSIONS  50
#endif
#ifndef TORK_ARCHIVE_DIR
#define TORK_ARCHIVE_DIR    TORK_PERSIST_DIR "/code_archive"
#endif

/* ── 内核循环 ────────────────────────────────────────────── */
#ifndef TORK_IDLE_MS
#define TORK_IDLE_MS        100     /* 空闲时 100ms 一次 tick */
#endif
#ifndef TORK_ACTIVE_MS
#define TORK_ACTIVE_MS      10      /* 活跃时 10ms 一次 tick */
#endif
#ifndef TORK_DEFAULT_STRESS
#define TORK_DEFAULT_STRESS 50
#endif

/* ── 安全 ────────────────────────────────────────────────── */
#ifndef TORK_MAX_FILE_SIZE
#define TORK_MAX_FILE_SIZE  (10 * 1024 * 1024)  /* 10MB */
#endif
#ifndef TORK_WHITELIST_CMD_MAX
#define TORK_WHITELIST_CMD_MAX  50
#endif

/* ── 辅助宏 ──────────────────────────────────────────────── */
#define TORK_VERSION_STR(major, minor, patch) \
    #major "." #minor "." #patch

#ifdef __cplusplus
}
#endif

#endif /* TORK_CONFIG_H */
