/* ══════════════════════════════════════════════════════════════
 * TORK 轻量 PBFT 共识 — 分布式拜占庭容错
 *
 * 在 T2T Mesh 中, 多个 TORK 实例需要对"谁的健康数据可信"达成共识。
 * 完整的 PBFT 太复杂 (3000+ 行), 这里实现一个轻量版:
 *
 * 核心假设:
 *   1. 节点数 N ≤ 16 (T2T Mesh 上限 32, 共识节点取半数)
 *   2. 拜占庭节点 f ≤ (N-1)/3
 *   3. 所有节点已知彼此公钥 (HMAC 密钥)
 *
 * 共识流程 (简化 PBFT):
 *   1. Pre-Prepare: 主节点提议一个值
 *   2. Prepare: 收到提议后广播 prepare
 *   3. Commit: 收到 2f+1 个 prepare 后广播 commit
 *   4. Reply: 收到 2f+1 个 commit 后输出共识结果
 *
 * 视图更换 (View Change):
 *   如果主节点超时, 自动切换到下一个视图
 * ══════════════════════════════════════════════════════════════ */

#ifndef TORK_PBFT_H
#define TORK_PBFT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 常量 ──────────────────────────────────────────────── */
#define PBFT_MAX_NODES     16
#define PBFT_MAX_MSG      512

/* ── 消息类型 ──────────────────────────────────────────── */
typedef enum {
    PBFT_PRE_PREPARE = 0,
    PBFT_PREPARE     = 1,
    PBFT_COMMIT      = 2,
    PBFT_REPLY       = 3,
    PBFT_VIEW_CHANGE = 4,
    PBFT_NEW_VIEW    = 5,
} pbft_msg_type_t;

/* ── 共识消息 ──────────────────────────────────────────── */
typedef struct {
    pbft_msg_type_t type;
    uint8_t         view;          /* 视图号 */
    uint8_t         seq;           /* 序列号 */
    uint8_t         sender_id;     /* 发送节点 ID */
    uint8_t         digest[32];    /* 提议值 SHA256 摘要 */
    uint8_t         signature[32]; /* HMAC 签名 */
    uint32_t        timestamp_ms;
} pbft_message_t;

/* ── 共识状态 ──────────────────────────────────────────── */
typedef struct {
    uint8_t  node_id;         /* 本节点 ID */
    uint8_t  total_nodes;     /* 总节点数 */
    uint8_t  current_view;    /* 当前视图 */
    uint8_t  seq_num;         /* 当前序列号 */
    uint8_t  primary_id;      /* 当前主节点 ID */

    /* 准备和提交计数 */
    uint8_t  prepare_count[PBFT_MAX_NODES];
    uint8_t  commit_count[PBFT_MAX_NODES];

    /* 已收到的消息 (位图) */
    uint32_t prepared_bitmap;  /* 位 i 表示收到节点 i 的 prepare */
    uint32_t committed_bitmap; /* 位 i 表示收到节点 i 的 commit */

    /* 共识结果 */
    uint8_t  consensus_reached;
    uint8_t  final_digest[32];
    uint64_t final_timestamp_ms;

    /* 超时 */
    uint32_t view_timeout_ms;  /* 视图超时 (默认 5000) */
    uint64_t view_start_ms;   /* 当前视图开始时间戳 */
} pbft_state_t;

/* ── API ───────────────────────────────────────────────── */

/* 初始化 PBFT 状态 */
void pbft_init(pbft_state_t *state, uint8_t node_id, uint8_t total_nodes);

/* 主节点发起提议: 返回 0=成功 */
int  pbft_propose(pbft_state_t *state, const uint8_t *digest, uint32_t digest_len);

/* 收到消息: 返回 0=正常, 1=共识达成, <0=错误 */
int  pbft_handle_message(pbft_state_t *state, const pbft_message_t *msg);

/* 检查是否达成共识 */
int  pbft_is_consensus_reached(pbft_state_t *state);

/* 视图更换: 超时或主节点故障 */
int  pbft_view_change(pbft_state_t *state);

/* 获取当前主节点 ID */
uint8_t pbft_primary(pbft_state_t *state);

/* 重置状态 (开始新共识) */
void pbft_reset(pbft_state_t *state);

/* 容错计算: 给定 N, 返回最大容忍的拜占庭节点数 */
int  pbft_max_byzantine(int total_nodes);

#ifdef __cplusplus
}
#endif

#endif /* TORK_PBFT_H */
