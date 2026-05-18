#include "bridge_integration.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* ── 初始化 ────────────────────────────────────────────── */
void bridge_init(tork_bridge_t *br, const char *host, int port, int use_ws) {
    memset(br, 0, sizeof(*br));
    br->enabled = (host && host[0]);

    if (!br->enabled) {
        snprintf(br->summary, sizeof(br->summary), "SP bridge: disabled");
        return;
    }

    /* 填充配置 */
    snprintf(br->config.host, sizeof(br->config.host), "%s", host);
    br->config.port = port > 0 ? port : SP_DEFAULT_PORT;
    br->config.use_ws = use_ws;
    br->config.reconnect_ms = SP_DEFAULT_RECONNECT;
    br->config.timeout_ms = SP_DEFAULT_TIMEOUT;

    /* 初始化状态 */
    sp_bridge_init(&br->state, &br->config);

    /* 首次连接 */
    if (sp_bridge_connect(&br->state, &br->config) == 0) {
        snprintf(br->summary, sizeof(br->summary),
                 "SP bridge: connected to %s:%d (ws=%s)",
                 br->config.host, br->config.port,
                 br->config.use_ws ? "yes" : "no");
    } else {
        snprintf(br->summary, sizeof(br->summary),
                 "SP bridge: waiting... %s", br->state.last_error);
    }
}

/* ── 处理入站消息 ───────────────────────────────────────── */
void bridge_process_incoming(tork_bridge_t *br) {
    if (!br->enabled || !br->state.connected) return;

    char buf[SP_BUF_SIZE];
    while (1) {
        int n = sp_bridge_recv(&br->state, buf, sizeof(buf));
        if (n <= 0) break;

        /* 解析入站消息类型，路由到对应 handler */
        /* 简单 JSON 解析: 查找 "type" 字段 */
        int msg_type = -1;
        const char *t = strstr(buf, "\"type\"");
        if (t) {
            const char *v = strchr(t, ':');
            if (v) {
                while (*v && (*v < '0' || *v > '9')) v++;
                if (*v) msg_type = atoi(v);
            }
        }

        switch (msg_type) {
        case SP_MSG_EXEC: {
            /* SP → TORK: 执行命令 */
            const char *cmd = strstr(buf, "\"cmd\"");
            if (cmd) {
                const char *start = strchr(cmd, ':');
                if (start) {
                    while (*start && *start != '"') start++;
                    if (*start) {
                        start++;
                        char exec_cmd[256];
                        int ci = 0;
                        while (*start && *start != '"' && ci < (int)sizeof(exec_cmd)-1)
                            exec_cmd[ci++] = *start++;
                        exec_cmd[ci] = '\0';
                        /* 执行结果会通过下次心跳发送回去 */
                        /* 这里只记录，实际执行在 dispatch */
                    }
                }
            }
            break;
        }
        case SP_MSG_QUERY: {
            /* SP → TORK: 查询请求 */
            break;
        }
        case SP_MSG_EVOLVE: {
            /* SP → TORK: 进化指令 */
            break;
        }
        case SP_MSG_MENTOR: {
            /* SP → TORK: 师徒阶段同步 */
            break;
        }
        default:
            break;
        }
    }
}

/* ── 心跳 ──────────────────────────────────────────────── */
void bridge_tick(tork_bridge_t *br, const void *soul,
                 int tick, int drive, int stress, int gen,
                 float fear, float desire, float curiosity) {
    if (!br->enabled) return;

    /* 检查重连 */
    if (!br->state.connected) {
        if (sp_bridge_should_reconnect(&br->state, &br->config)) {
            if (sp_bridge_connect(&br->state, &br->config) == 0) {
                snprintf(br->summary, sizeof(br->summary),
                         "SP bridge: reconnected to %s:%d",
                         br->config.host, br->config.port);
            }
            br->state.reconnect_count++;
        }
        return;
    }

    /* 处理入站 */
    bridge_process_incoming(br);

    /* 发送状态心跳 */
    sp_bridge_send_status(&br->state, soul, tick, drive, stress, gen);

    /* 每 5 tick 发送一次本能数据 */
    if (tick % 5 == 0) {
        sp_bridge_send_instinct(&br->state, fear, desire, curiosity);
    }

    /* 每 20 tick 发送 soul 快照 */
    if (tick % 20 == 0 && soul) {
        /* soul size 在外部决定，用 96 作为标准 */
        sp_bridge_send_soul(&br->state, soul, 96);
    }
}

/* ── 发送 JSON ─────────────────────────────────────────── */
int bridge_send_json(tork_bridge_t *br, const char *json) {
    if (!br->enabled || !br->state.connected) return -1;
    return sp_bridge_send(&br->state, json);
}

/* ── 摘要 ──────────────────────────────────────────────── */
const char* bridge_summary(tork_bridge_t *br) {
    if (!br->enabled) return "SP bridge: disabled";
    if (br->state.connected) {
        snprintf(br->summary, sizeof(br->summary),
                 "SP bridge: connected (s=%d r=%d ws=%s)",
                 br->state.msg_sent, br->state.msg_recv,
                 br->state.ws_mode ? "yes" : "no");
    }
    return br->summary;
}

/* ── 清理 ──────────────────────────────────────────────── */
void bridge_shutdown(tork_bridge_t *br) {
    sp_bridge_disconnect(&br->state);
    br->enabled = 0;
}
