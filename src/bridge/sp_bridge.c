#include "sp_bridge.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

/* ── 辅助工具 ────────────────────────────────────────────── */

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static int set_nonblock(int fd, int nonblock) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (nonblock) flags |= O_NONBLOCK;
    else flags &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}

/* 简易 JSON 转义 */
static void json_escape(const char *src, char *dst, int dst_size) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 2; i++) {
        switch (src[i]) {
        case '"':  dst[j++] = '\\'; dst[j++] = '"';  break;
        case '\\': dst[j++] = '\\'; dst[j++] = '\\'; break;
        case '\n': dst[j++] = '\\'; dst[j++] = 'n';  break;
        case '\r': dst[j++] = '\\'; dst[j++] = 'r';  break;
        case '\t': dst[j++] = '\\'; dst[j++] = 't';  break;
        default:   dst[j++] = src[i]; break;
        }
    }
    dst[j] = '\0';
}

/* ── 初始化 ────────────────────────────────────────────── */
void sp_bridge_init(sp_bridge_state_t *state, const sp_bridge_config_t *cfg) {
    (void)cfg;
    memset(state, 0, sizeof(*state));
    state->fd = -1;
    state->last_heartbeat_ms = now_ms();
}

/* ── TCP 连接 ──────────────────────────────────────────── */
static int tcp_connect(const char *host, int port, int timeout_ms) {
    struct addrinfo hints, *res, *rp;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0) return -1;

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        set_nonblock(fd, 1);
        connect(fd, rp->ai_addr, rp->ai_addrlen);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(fd, &wset);
        int ret = select(fd + 1, NULL, &wset, NULL, &tv);
        if (ret <= 0) { close(fd); fd = -1; continue; }

        int so_error = 0;
        socklen_t len = sizeof(so_error);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (so_error != 0) { close(fd); fd = -1; continue; }

        set_nonblock(fd, 0);
        break;
    }
    freeaddrinfo(res);
    return fd;
}

/* ── WebSocket 握手 ────────────────────────────────────── */
static int ws_handshake(int fd, const char *host) {
    /* 生成随机 Sec-WebSocket-Key (base64 编码的 16 字节) */
    unsigned char key_raw[16];
    for (int i = 0; i < 16; i++) key_raw[i] = rand() & 0xFF;
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char key_b64[25];
    for (int i = 0; i < 24; i++) {
        int idx = (i < 16) ? (key_raw[i] >> 2) : 0;
        if (i < 16) key_b64[i] = b64[key_raw[i] & 0x3F];
        else key_b64[i] = '=';
    }
    key_b64[24] = '\0';

    char req[1024];
    int req_len = snprintf(req, sizeof(req),
        "GET /relay HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Protocol: silent-protocol-v1\r\n"
        "\r\n", host, key_b64);

    if (write(fd, req, req_len) != req_len) return -1;

    char resp[1024];
    int total = 0;
    while (total < (int)sizeof(resp) - 1) {
        ssize_t n = read(fd, resp + total, sizeof(resp) - 1 - total);
        if (n <= 0) return -1;
        total += n;
        resp[total] = '\0';
        if (strstr(resp, "\r\n\r\n")) break;
    }

    if (!strstr(resp, "101 Switching Protocols")) return -1;
    return 0;
}

/* ── WebSocket 帧编解码 ─────────────────────────────────── */
static int ws_encode_frame(const char *payload, int payload_len,
                            char *frame, int frame_cap, int is_text) {
    if (frame_cap < payload_len + 10) return -1;
    int pos = 0;
    frame[pos++] = 0x80 | (is_text ? 0x01 : 0x02); /* FIN + opcode */

    if (payload_len < 126) {
        frame[pos++] = payload_len;
    } else if (payload_len < 65536) {
        frame[pos++] = 126;
        frame[pos++] = (payload_len >> 8) & 0xFF;
        frame[pos++] = payload_len & 0xFF;
    } else {
        frame[pos++] = 127;
        for (int i = 7; i >= 0; i--)
            frame[pos++] = (payload_len >> (i * 8)) & 0xFF;
    }

    memcpy(frame + pos, payload, payload_len);
    return pos + payload_len;
}

static int ws_decode_frame(const unsigned char *frame, int frame_len,
                            char *payload, int payload_cap) {
    if (frame_len < 2) return -1;
    int masked = (frame[1] & 0x80) ? 1 : 0;
    int payload_len = frame[1] & 0x7F;
    int header_len = 2 + (masked ? 4 : 0);

    if (payload_len == 126) {
        if (frame_len < 4) return -1;
        payload_len = (frame[2] << 8) | frame[3];
        header_len = 4 + (masked ? 4 : 0);
    } else if (payload_len == 127) {
        if (frame_len < 10) return -1;
        payload_len = 0;
        for (int i = 0; i < 8; i++)
            payload_len = (payload_len << 8) | frame[2 + i];
        header_len = 10 + (masked ? 4 : 0);
    }

    if (frame_len < header_len + payload_len) return -1;
    if (payload_len > payload_cap - 1) return -1;

    const unsigned char *data = frame + header_len;
    if (masked) {
        const unsigned char *mask = frame + header_len - 4;
        for (int i = 0; i < payload_len; i++)
            payload[i] = data[i] ^ mask[i & 3];
    } else {
        memcpy(payload, data, payload_len);
    }
    payload[payload_len] = '\0';
    return payload_len;
}

/* ── 连接 / 断开 ────────────────────────────────────────── */
int sp_bridge_connect(sp_bridge_state_t *state, const sp_bridge_config_t *cfg) {
    if (state->connected) return 0;

    int fd = tcp_connect(cfg->host, cfg->port, cfg->timeout_ms);
    if (fd < 0) {
        snprintf(state->last_error, sizeof(state->last_error),
                 "connect failed: %s:%d", cfg->host, cfg->port);
        return -1;
    }

    if (cfg->use_ws) {
        if (ws_handshake(fd, cfg->host) < 0) {
            close(fd);
            snprintf(state->last_error, sizeof(state->last_error),
                     "WebSocket handshake failed");
            return -1;
        }
        state->ws_mode = 1;
    }

    state->fd = fd;
    state->connected = 1;
    state->reconnect_count = 0;
    state->last_heartbeat_ms = now_ms();
    return 0;
}

void sp_bridge_disconnect(sp_bridge_state_t *state) {
    if (state->fd >= 0) {
        close(state->fd);
        state->fd = -1;
    }
    state->connected = 0;
}

/* ── 发送 ──────────────────────────────────────────────── */
int sp_bridge_send(sp_bridge_state_t *state, const char *json_msg) {
    if (!state->connected || state->fd < 0) return -1;

    int len = strlen(json_msg);
    int ret;

    if (state->ws_mode) {
        char frame[SP_BUF_SIZE];
        int frame_len = ws_encode_frame(json_msg, len, frame, sizeof(frame), 1);
        if (frame_len < 0) return -1;
        ret = write(state->fd, frame, frame_len);
    } else {
        /* TCP 直连模式：发送 JSON + \n */
        char buf[SP_BUF_SIZE];
        int total = snprintf(buf, sizeof(buf), "%s\n", json_msg);
        if (total >= (int)sizeof(buf)) return -1;
        ret = write(state->fd, buf, total);
    }

    if (ret > 0) {
        state->msg_sent++;
        return 0;
    }
    return -1;
}

/* ── 接收 (非阻塞) ──────────────────────────────────────── */
int sp_bridge_recv(sp_bridge_state_t *state, char *buf, size_t cap) {
    if (!state->connected || state->fd < 0 || !buf || cap == 0) return -1;

    memset(buf, 0, cap);

    if (state->ws_mode) {
        unsigned char raw[SP_BUF_SIZE];
        ssize_t n = read(state->fd, raw, sizeof(raw) - 1);
        if (n <= 0) return -1;
        int plen = ws_decode_frame(raw, (int)n, buf, (int)cap - 1);
        if (plen <= 0) return -1;
        buf[plen] = '\0';
        state->msg_recv++;
        return plen;
    } else {
        /* TCP 模式：读一行 */
        ssize_t n = read(state->fd, buf, cap - 1);
        if (n <= 0) return -1;
        buf[n] = '\0';
        /* 去掉尾部 \r\n */
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
        state->msg_recv++;
        return (int)n;
    }
}

/* ── 构造 SP 标准消息 ────────────────────────────────────── */
int sp_bridge_send_status(sp_bridge_state_t *state, const void *soul,
                           int tick, int drive, int stress, int gen) {
    char json[SP_BUF_SIZE];
    snprintf(json, sizeof(json),
        "{\"type\":%d,\"source\":\"tork\",\"tick\":%d,\"drive\":%d,"
        "\"stress\":%d,\"gen\":%d,\"ts\":%llu}",
        SP_MSG_STATUS, tick, drive, stress, gen,
        (unsigned long long)now_ms());
    return sp_bridge_send(state, json);
}

int sp_bridge_send_soul(sp_bridge_state_t *state, const void *soul, int size) {
    /* Hex encode soul data */
    char hex[1024];
    int pos = 0;
    const unsigned char *s = (const unsigned char*)soul;
    for (int i = 0; i < size && pos < (int)sizeof(hex) - 4; i++)
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x", s[i]);

    char json[2048];
    snprintf(json, sizeof(json),
        "{\"type\":%d,\"source\":\"tork\",\"soul_hex\":\"%s\",\"ts\":%llu}",
        SP_MSG_SOUL, hex, (unsigned long long)now_ms());
    return sp_bridge_send(state, json);
}

int sp_bridge_send_instinct(sp_bridge_state_t *state,
                             float fear, float desire, float curiosity) {
    char json[SP_BUF_SIZE];
    snprintf(json, sizeof(json),
        "{\"type\":%d,\"source\":\"tork\",\"fear\":%.4f,\"desire\":%.4f,"
        "\"curiosity\":%.4f,\"ts\":%llu}",
        SP_MSG_INSTINCT, fear, desire, curiosity,
        (unsigned long long)now_ms());
    return sp_bridge_send(state, json);
}

/* ── 重连检查 ──────────────────────────────────────────── */
int sp_bridge_should_reconnect(const sp_bridge_state_t *state,
                                const sp_bridge_config_t *cfg) {
    if (state->connected) return 0;
    if (state->reconnect_count > 10) return 0;  /* 最多重连 10 次 */
    uint64_t elapsed = now_ms() - state->last_heartbeat_ms;
    return (elapsed >= (uint64_t)cfg->reconnect_ms) ? 1 : 0;
}

/* ── 状态摘要 ──────────────────────────────────────────── */
void sp_bridge_summary(const sp_bridge_state_t *state, char *buf, size_t cap) {
    snprintf(buf, cap,
        "SP bridge: %s | sent=%d recv=%d reconnect=%d ws=%s",
        state->connected ? "connected" : "disconnected",
        state->msg_sent, state->msg_recv, state->reconnect_count,
        state->ws_mode ? "yes" : "no");
}
