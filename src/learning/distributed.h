#ifndef DISTRIBUTED_H
#define DISTRIBUTED_H

/* ── 分布式黑板协议 — TORK 间经验交换 ─────────────────────
 *  基于 UDP 多播，无中心服务器，纯 P2P。
 *  每个 TORK 实例定期广播自己的高价值经验/模式/基因，
 *  同时监听网络上的其他 TORK 实例，可选吸收外来知识。
 *
 *  设计哲学：像蚂蚁释放信息素。广播即忘，接收自选。
 *  没有握手，没有确认，没有重传。
 * ──────────────────────────────────────────────────────── */

#include <stdint.h>

/* ── 网络配置 ─────────────────────────────────────────── */
#define DIST_MCAST_GROUP    "239.42.69.42"
#define DIST_MCAST_PORT     42069
#define DIST_MAX_MSG        1400   /* < 1500 MTU, safe for UDP */
#define DIST_APP_ID         0x544F524B  /* "TORK" */
#define DIST_TOKEN          0x544B4E47  /* "TKNG" — shared group token for auth */

/* ── 消息类型 ─────────────────────────────────────────── */
#define DIST_MSG_HEARTBEAT  0x01   /* 心跳/宣告存在 */
#define DIST_MSG_EXPERIENCE 0x02   /* 经验分享 */
#define DIST_MSG_PATTERN    0x03   /* 学到的模式 */
#define DIST_MSG_BRANCH     0x04   /* 高适应度分支基因 */

/* ── 消息头 ────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t magic;           /* DIST_APP_ID */
    uint32_t token;           /* DIST_TOKEN — shared group auth */
    uint8_t  version;         /* 协议版本 = 1 */
    uint8_t  msg_type;        /* DIST_MSG_* */
    uint32_t instance_id;     /* 发送者实例ID (随机生成) */
    uint32_t gen_count;       /* 发送者世代 */
    uint16_t payload_len;     /* 负载长度 (紧随头之后) */
    uint32_t crc32;           /* 整个消息的校验 */
} dist_header_t;

#define DIST_HEADER_SIZE  sizeof(dist_header_t)  /* 20 bytes */

/* ── 负载类型 ────────────────────────────────────────────── */

/* Heartbeat (无负载) */

/* Experience 负载 */
typedef struct __attribute__((packed)) {
    uint8_t  hw_stress;       /* 当时压力 */
    int8_t   drive_pre;       /* 行动前驱动 */
    uint8_t  action_type;     /* 行动类型 */
    int8_t   action_param;    /* 行动参数 */
    int8_t   outcome;         /* 结果 (-100..100) */
    uint8_t  crash;           /* 是否崩溃 */
} dist_experience_t;  /* 6 bytes */

/* Pattern 负载 */
typedef struct __attribute__((packed)) {
    uint8_t  stress_low;      /* 模式适用压力区间 */
    uint8_t  stress_high;
    int8_t   drive_min;
    int8_t   drive_max;
    uint8_t  action_type;     /* 推荐行动 */
    int8_t   avg_outcome;     /* 平均结果 */
    uint16_t sample_count;    /* 样本数 */
} dist_pattern_t;  /* 10 bytes */

/* Branch 负载 (高适应度分支的基因) */
typedef struct __attribute__((packed)) {
    int32_t  curiosity_decay; /* fixed-point *1000 */
    int32_t  learning_rate;   /* fixed-point *1000 */
    int32_t  peak_drive;      /* 分支达到的最高驱动值 */
    uint16_t ticks_lived;     /* 分支存活时长 */
} dist_branch_t;  /* 16 bytes */

/* ── 公共 API ──────────────────────────────────────────── */

/* 初始化分布式黑板 (创建 UDP socket, 加入多播组) */
/* 返回 0 成功, -1 失败 (无网络时静默失败) */
int dist_init(void);

/* 每 tick 调用: 非阻塞接收 + 自动集成外来知识 */
/* 集成策略: 经验直接加入本地缓冲区, 模式若置信度高则采纳 */
void dist_tick(void);

/* 广播一条经验 (由 engine 在重要事件后调用) */
void dist_broadcast_experience(uint8_t hw_stress, int8_t drive_pre,
                                uint8_t action_type, int8_t action_param,
                                int8_t outcome, uint8_t crash);

/* 广播一个模式 (由 pattern 模块在学到新规律时调用) */
void dist_broadcast_pattern(uint8_t stress_low, uint8_t stress_high,
                             int8_t drive_min, int8_t drive_max,
                             uint8_t action_type, int8_t avg_outcome,
                             uint16_t sample_count);

/* 广播一个高适应度分支基因 (由 branch 模块在收割时调用) */
void dist_broadcast_branch(int32_t curiosity_decay, int32_t learning_rate,
                            int32_t peak_drive, uint16_t ticks_lived);

/* 发送心跳 (宣告自身存在) */
void dist_heartbeat(void);

/* 获取已知的远程实例数量 */
int dist_peer_count(void);

/* ── 合并策略 ────────────────────────────────────────── */

/* 合并决策: 是否应接受远程经验
 * local_gen: 本地世代
 * remote_gen: 远程世代
 * remote_confidence: 远程置信度 (0.0~1.0)
 * local_success_rate: 本地的同类型动作成功率 (0.0~1.0)
 * 返回: 0=拒绝, 1=接受, 2=接受并优先 */
int dist_merge_decision(int local_gen, int remote_gen,
                         float remote_confidence,
                         float local_success_rate);

/* 合并两条经验: 返回融合后的结果
 * local_outcome: 本地的经验结果 (-100..100)
 * remote_outcome: 远程的经验结果 (-100..100)
 * remote_confidence: 远程置信度
 * merge_weight: 远程权重 (由 merge_decision 决定, 0.0~1.0)
 * 返回融合后的结果 */
int dist_merge_outcomes(int local_outcome, int remote_outcome,
                         float remote_confidence,
                         float merge_weight);

/* 获取远程实例的世代统计 */
void dist_peer_stats(int *count, int *max_gen, float *avg_confidence);

/* 清理 (离开多播组, 关闭 socket) */
void dist_cleanup(void);

#endif /* DISTRIBUTED_H */
