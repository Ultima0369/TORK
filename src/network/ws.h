#ifndef TORK_WS_H
#define TORK_WS_H

#include <stdint.h>

/* TORK WebSocket 服务器
 * 轻量级 WebSocket (RFC 6455) 实现
 * 允许浏览器 / 外部工具直接连接 TORK
 */

#define WS_MAX_CLIENTS 8
#define WS_BUF_SIZE    4096

typedef enum {
    WS_OP_CONTINUATION = 0x0,
    WS_OP_TEXT         = 0x1,
    WS_OP_BINARY       = 0x2,
    WS_OP_CLOSE        = 0x8,
    WS_OP_PING         = 0x9,
    WS_OP_PONG         = 0xA,
} ws_opcode_t;

typedef struct {
    int fd;
    int active;
    uint64_t last_pong;
    uint8_t buf[WS_BUF_SIZE];
    int buf_len;
} ws_client_t;

typedef struct {
    int server_fd;
    int port;
    ws_client_t clients[WS_MAX_CLIENTS];
    int running;
    void (*on_message)(int client_id, const uint8_t *data, int len, ws_opcode_t op);
} ws_server_t;

/* 启动 WebSocket 服务器 (端口，消息回调) */
int ws_start(ws_server_t *srv, int port, void (*on_msg)(int, const uint8_t *, int, ws_opcode_t));

/* 每帧轮询 (在主循环中调用) */
void ws_poll(ws_server_t *srv);

/* 向指定客户端发送消息 */
int ws_send(ws_server_t *srv, int client_id, const uint8_t *data, int len, ws_opcode_t op);

/* 广播给所有客户端 */
int ws_broadcast(ws_server_t *srv, const uint8_t *data, int len, ws_opcode_t op);

/* 停止服务器 */
void ws_stop(ws_server_t *srv);

#endif /* TORK_WS_H */
