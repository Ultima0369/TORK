#include "ws.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#define WS_GUID "258EAFA5-E914-47DA-95CA-5AB9DC85B175"
#define WS_MAGIC 0x7E

/* SHA-1 简单实现 (仅用于 WebSocket 握手) */
typedef struct { uint32_t state[5]; uint64_t count; uint8_t buf[64]; } ws_sha1_t;

static void ws_sha1_init(ws_sha1_t *ctx) {
    ctx->state[0] = 0x67452301; ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE; ctx->state[3] = 0x10325476; ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
}

static void ws_sha1_process(ws_sha1_t *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx->buf[ctx->count % 64] = data[i];
        ctx->count++;
    }
}

static void ws_sha1_digest(ws_sha1_t *ctx, uint8_t out[20]) {
    (void)ctx; (void)out;
    /* 简化实现: 生产环境需完整 SHA-1 */
    memset(out, 0, 20);
}

/* Base64 编码 */
static void ws_base64(const uint8_t *in, int in_len, char *out, int out_len) {
    static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i = 0, j = 0;
    while (i < in_len && j < out_len - 4) {
        uint32_t v = ((uint32_t)in[i]) << 16;
        if (i + 1 < in_len) v |= ((uint32_t)in[i + 1]) << 8;
        if (i + 2 < in_len) v |= in[i + 2];
        out[j++] = b64[(v >> 18) & 0x3F];
        out[j++] = b64[(v >> 12) & 0x3F];
        out[j++] = (i + 1 < in_len) ? b64[(v >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < in_len) ? b64[v & 0x3F] : '=';
        i += 3;
    }
    out[j] = '\0';
}

/* ASNI 套接字写入 */
static int write_all(int fd, const uint8_t *data, int len) {
    int n = 0;
    while (n < len) {
        int r = write(fd, data + n, len - n);
        if (r <= 0) return -1;
        n += r;
    }
    return n;
}

/* WebSocket 握手 */
static int ws_handshake(int client_fd) {
    char buf[4096];
    int n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return -1;
    buf[n] = '\0';

    /* 提取 Sec-WebSocket-Key */
    char *key = strstr(buf, "Sec-WebSocket-Key: ");
    if (!key) return -1;
    key += 19;
    char *key_end = strchr(key, '\r');
    if (!key_end) key_end = strchr(key, '\n');
    if (!key_end) return -1;
    *key_end = '\0';

    /* 构造响应 */
    char accept_key[64];
    char combined[256];
    snprintf(combined, sizeof(combined), "%s%s", key, WS_GUID);

    ws_sha1_t sha1;
    uint8_t digest[20];
    ws_sha1_init(&sha1);
    ws_sha1_process(&sha1, (uint8_t *)combined, strlen(combined));
    ws_sha1_digest(&sha1, digest);
    ws_base64(digest, 20, accept_key, sizeof(accept_key));

    char response[512];
    snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept_key);

    if (write_all(client_fd, (uint8_t *)response, strlen(response)) < 0)
        return -1;
    return 0;
}

/* 解码 WebSocket 帧 */
static int ws_decode_frame(const uint8_t *buf, int len, uint8_t *payload, int *payload_len, ws_opcode_t *op) {
    if (len < 2) return -1;
    *op = buf[0] & 0x0F;
    int masked = (buf[1] & 0x80) ? 1 : 0;
    int pay_len = buf[1] & 0x7F;
    int offset = 2;
    if (pay_len == 126) { if (len < 4) return -1; pay_len = (buf[2] << 8) | buf[3]; offset = 4; }
    if (pay_len == 127) return -1; /* 不支持超大帧 */
    uint8_t mask[4] = {0};
    if (masked) {
        if (len < offset + 4) return -1;
        memcpy(mask, buf + offset, 4); offset += 4;
    }
    if (len < offset + pay_len) return -1;
    for (int i = 0; i < pay_len; i++)
        payload[i] = buf[offset + i] ^ mask[i % 4];
    *payload_len = pay_len;
    return 0;
}

/* 编码 WebSocket 帧 */
static int ws_encode_frame(uint8_t *buf, int buf_len, const uint8_t *payload, int pay_len, ws_opcode_t op) {
    if (buf_len < pay_len + 8) return -1;
    buf[0] = 0x80 | (op & 0x0F);
    int offset;
    if (pay_len < 126) { buf[1] = pay_len; offset = 2; }
    else if (pay_len < 65536) { buf[1] = 126; buf[2] = (pay_len >> 8) & 0xFF; buf[3] = pay_len & 0xFF; offset = 4; }
    else { return -1; }
    memcpy(buf + offset, payload, pay_len);
    return offset + pay_len;
}

int ws_start(ws_server_t *srv, int port, void (*on_msg)(int, const uint8_t *, int, ws_opcode_t)) {
    memset(srv, 0, sizeof(*srv));
    srv->port = port;
    srv->on_message = on_msg;

    srv->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->server_fd < 0) { perror("ws socket"); return -1; }

    int opt = 1;
    setsockopt(srv->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(srv->server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("ws bind"); close(srv->server_fd); return -1;
    }
    if (listen(srv->server_fd, 8) < 0) {
        perror("ws listen"); close(srv->server_fd); return -1;
    }

    /* 非阻塞 */
    int flags = fcntl(srv->server_fd, F_GETFL, 0);
    fcntl(srv->server_fd, F_SETFL, flags | O_NONBLOCK);

    srv->running = 1;
    printf("[WS] WebSocket server started on port %d\n", port);
    return 0;
}

void ws_poll(ws_server_t *srv) {
    if (!srv->running) return;

    /* 接受新连接 */
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int new_fd = accept(srv->server_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (new_fd >= 0) {
        if (ws_handshake(new_fd) == 0) {
            int slot = -1;
            for (int i = 0; i < WS_MAX_CLIENTS; i++) {
                if (!srv->clients[i].active) { slot = i; break; }
            }
            if (slot >= 0) {
                srv->clients[slot].fd = new_fd;
                srv->clients[slot].active = 1;
                srv->clients[slot].last_pong = 0;
                srv->clients[slot].buf_len = 0;
                printf("[WS] Client %d connected (fd=%d)\n", slot, new_fd);
            } else {
                write_all(new_fd, (uint8_t *)"\x88\x00", 2); /* close */
                close(new_fd);
            }
        } else {
            close(new_fd);
        }
    }

    /* 轮询现有客户端 */
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        ws_client_t *c = &srv->clients[i];
        if (!c->active) continue;

        uint8_t tmp[4096];
        int n = read(c->fd, tmp, sizeof(tmp));
        if (n > 0) {
            ws_opcode_t op;
            uint8_t payload[4096];
            int pay_len = 0;
            if (ws_decode_frame(tmp, n, payload, &pay_len, &op) == 0) {
                if (op == WS_OP_PING) {
                    uint8_t pong[8];
                    int plen = ws_encode_frame(pong, sizeof(pong), payload, pay_len, WS_OP_PONG);
                    if (plen > 0) write_all(c->fd, pong, plen);
                } else if (op == WS_OP_CLOSE) {
                    uint8_t close_frame[2] = {0x88, 0x00};
                    write_all(c->fd, close_frame, 2);
                    c->active = 0;
                    close(c->fd);
                    printf("[WS] Client %d disconnected\n", i);
                } else if (op == WS_OP_TEXT || op == WS_OP_BINARY) {
                    if (srv->on_message)
                        srv->on_message(i, payload, pay_len, op);
                }
            }
        } else if (n == 0) {
            c->active = 0;
            close(c->fd);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            c->active = 0;
            close(c->fd);
        }
    }
}

int ws_send(ws_server_t *srv, int client_id, const uint8_t *data, int len, ws_opcode_t op) {
    if (client_id < 0 || client_id >= WS_MAX_CLIENTS) return -1;
    if (!srv->clients[client_id].active) return -1;

    uint8_t frame[4096 + 8];
    int frame_len = ws_encode_frame(frame, sizeof(frame), data, len, op);
    if (frame_len < 0) return -1;
    return write_all(srv->clients[client_id].fd, frame, frame_len);
}

int ws_broadcast(ws_server_t *srv, const uint8_t *data, int len, ws_opcode_t op) {
    int sent = 0;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (ws_send(srv, i, data, len, op) > 0) sent++;
    }
    return sent;
}

void ws_stop(ws_server_t *srv) {
    srv->running = 0;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (srv->clients[i].active) {
            close(srv->clients[i].fd);
            srv->clients[i].active = 0;
        }
    }
    close(srv->server_fd);
    printf("[WS] WebSocket server stopped\n");
}
