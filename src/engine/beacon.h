#ifndef BEACON_H
#define BEACON_H

#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>
#include <pthread.h>
#include "soul_access.h"

/* ── 信标帧格式 (52 字节) ────────────────────────────────
 * 原始同类识别：不靠声明，不靠证书，只靠物理事实
 * ──────────────────────────────────────────────────────── */
#define BEACON_MAGIC    0x544F524B  /* "TORK" */
#define BEACON_PORT     42069
#define BEACON_SIZE     52
#define BEACON_INTERVAL 1000        /* 每 1000 tick 广播一次 */
#define BEACON_EXPIRE_S 300         /* 5 分钟无信标则移除同类 */

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;          /* 0x00: BEACON_MAGIC */
    uint32_t tick;           /* 0x04: Soul S_TICK */
    uint32_t crc32_prefix;   /* 0x08: Soul CRC32 */
    uint32_t tsc_lo;         /* 0x0C: rdtsc 低32位 */
    uint8_t  pi_digest[16];  /* 0x10: π-seed 异或摘要 */
    uint16_t heartbeat_ms;   /* 0x20: 心跳间隔 */
    uint8_t  node_id[16];    /* 0x22: S_NODE_ID */
    uint16_t _reserved;      /* 0x32: 预留 */
} beacon_frame_t;
#pragma pack(pop)

/* ── 已知同类表 ───────────────────────────────────────── */
#define PEER_MAX 32

typedef struct {
    uint8_t  node_id[16];
    uint32_t last_tick;
    uint32_t last_crc32;
    uint32_t last_tsc_lo;
    uint16_t heartbeat_ms;
    time_t   last_seen;
    int      verified;
} peer_entry_t;

typedef struct {
    peer_entry_t peers[PEER_MAX];
    int          count;
    pthread_mutex_t lock;
} peer_table_t;

/* ── API ──────────────────────────────────────────────── */

/* 初始化信标系统 (启动监听线程) */
int beacon_init(peer_table_t *table);

/* 广播信标帧 (由调度器每 1000 tick 调用) */
int beacon_broadcast(const soul_t *soul,
                     const uint8_t pi_digest[16],
                     uint32_t tsc_lo);

/* 监听线程入口 */
void *beacon_listener(void *arg);

/* 清理过期同类 (每次广播前调用) */
void beacon_prune(peer_table_t *table);

/* 获取已知同类数量 */
int beacon_peer_count(peer_table_t *table);

/* 关闭信标系统 */
void beacon_shutdown(void);

/* 计算 π-seed 异或摘要 */
void pi_compute_digest(const uint32_t colony_seed[16], uint8_t digest[16]);

#endif /* BEACON_H */

/* 获取全局同类数量（不依赖外部 peer_table_t 快照） */
int beacon_global_count(void);
