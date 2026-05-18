#ifndef SP_BRIDGE_H
#define SP_BRIDGE_H

#include <stdint.h>
#include <stddef.h>

/* ── Silent Protocol 双向桥接 ──────────────────────────────
 *  让 TORK 通过 Silent Protocol 与云端智能体通信。
 *
 *  架构:
 *    TORK ↔ sp_bridge ↔ SP Relay(HTTP/WS) ↔ cloud_ds/local_claude
 *
 *  传输格式: JSON over TCP (与 SP relay 兼容)
 *  自动检测: 支持 TCP 直连 和 WebSocket 升级两种模式
 * ───────────────────────────────────────────────────────── */

#define SP_HOST_MAX      64
#define SP_PATH_MAX      128
#define SP_BUF_SIZE      4096
#define SP_MAX_AGENTS    8

/* ── SP 消息类型 ───────────────────────────────────────── */
enum sp_msg_type {
    SP_MSG_PING       = 0,
    SP_MSG_STATUS     = 1,   /* TORK 状态报告 → SP */
    SP_MSG_SOUL       = 2,   /* Soul 快照 → SP */
    SP_MSG_EXEC       = 3,   /* SP → TORK 执行命令 */
    SP_MSG_QUERY      = 4,   /* SP → TORK 自然语言查询 */
    SP_MSG_RESULT     = 5,   /* TORK → SP 执行结果 */
    SP_MSG_EVOLVE     = 6,   /* SP → TORK 进化指令 */
    SP_MSG_INSTINCT   = 7,   /* TORK → SP 本能数据 */
    SP_MSG_MENTOR     = 8,   /* SP ↔ TORK 师徒阶段同步 */
    SP_MSG_ERROR      = 15,
};

/* ── SP 桥接配置 ───────────────────────────────────────── */
typedef struct {
    char host[SP_HOST_MAX];       /* SP relay 主机地址 */
    int  port;                     /* SP relay 端口 */
    int  use_ws;                   /* 是否使用 WebSocket */
    int  reconnect_ms;             /* 重连间隔 (ms) */
    int  timeout_ms;               /* 超时 (ms) */
    char agents[SP_MAX_AGENTS][SP_HOST_MAX]; /* 信任的 agent 列表 */
    int  agent_count;
} sp_bridge_config_t;

/* ── SP 桥接器状态 ─────────────────────────────────────── */
typedef struct {
    int  connected;
    int  fd;                       /* 连接 socket fd */
    char last_error[128];
    int  reconnect_count;
    int  msg_sent;
    int  msg_recv;
    int  ws_mode;                  /* 是否 WebSocket 模式 */
    uint64_t last_heartbeat_ms;    /* 上次心跳时间 */
} sp_bridge_state_t;

/* ── API ───────────────────────────────────────────────── */

/* 初始化桥接器 (不连接) */
void sp_bridge_init(sp_bridge_state_t *state, const sp_bridge_config_t *cfg);

/* 连接到 SP relay */
int sp_bridge_connect(sp_bridge_state_t *state, const sp_bridge_config_t *cfg);

/* 断开连接 */
void sp_bridge_disconnect(sp_bridge_state_t *state);

/* 发送 JSON 消息到 SP relay */
int sp_bridge_send(sp_bridge_state_t *state, const char *json_msg);

/* 接收 JSON 消息 (非阻塞) */
int sp_bridge_recv(sp_bridge_state_t *state, char *buf, size_t cap);

/* 构造并发送标准 SP 消息 */
int sp_bridge_send_status(sp_bridge_state_t *state, const void *soul,
                          int tick, int drive, int stress, int gen);
int sp_bridge_send_soul(sp_bridge_state_t *state, const void *soul, int size);
int sp_bridge_send_instinct(sp_bridge_state_t *state,
                            float fear, float desire, float curiosity);

/* 检查是否需要重连 */
int sp_bridge_should_reconnect(const sp_bridge_state_t *state,
                                const sp_bridge_config_t *cfg);

/* 获取桥接状态摘要 (用于 torkd status) */
void sp_bridge_summary(const sp_bridge_state_t *state, char *buf, size_t cap);

#endif /* SP_BRIDGE_H */
