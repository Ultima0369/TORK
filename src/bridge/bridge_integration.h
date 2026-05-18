#ifndef BRIDGE_INTEGRATION_H
#define BRIDGE_INTEGRATION_H

/* ── TORK 桥接集成 ─────────────────────────────────────────
 *  统一初始化、心跳、命令路由。
 *  调用方式: tork_engine 主循环中桥接 tick。
 * ───────────────────────────────────────────────────────── */

#include "sp_bridge.h"

/* ── 默认配置 ──────────────────────────────────────────── */
#define SP_DEFAULT_HOST       "127.0.0.1"
#define SP_DEFAULT_PORT       9876
#define SP_DEFAULT_RECONNECT  5000    /* 5秒重连间隔 */
#define SP_DEFAULT_TIMEOUT    3000    /* 3秒连接超时 */
#define SP_HEARTBEAT_INTERVAL 5000    /* 5秒发送一次心跳 */

/* ── 桥接上下文 ────────────────────────────────────────── */
typedef struct {
    sp_bridge_config_t config;
    sp_bridge_state_t  state;
    int enabled;
    char summary[256];
} tork_bridge_t;

/* ── API ───────────────────────────────────────────────── */

/* 初始化桥接 */
void bridge_init(tork_bridge_t *br, const char *host, int port, int use_ws);

/* 每次心跳调用: 维持连接 + 发送状态 + 处理入站消息 */
void bridge_tick(tork_bridge_t *br, const void *soul,
                 int tick, int drive, int stress, int gen,
                 float fear, float desire, float curiosity);

/* 处理入站 SP 消息 (内部调用，bridge_tick 已包含) */
void bridge_process_incoming(tork_bridge_t *br);

/* 发送自定义 JSON 消息 */
int bridge_send_json(tork_bridge_t *br, const char *json);

/* 获取桥接摘要 (在 torkd status 中显示) */
const char* bridge_summary(tork_bridge_t *br);

/* 断开并清理 */
void bridge_shutdown(tork_bridge_t *br);

#endif /* BRIDGE_INTEGRATION_H */
