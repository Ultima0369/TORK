#ifndef TORKD_BINARY_H
#define TORKD_BINARY_H

#include <stdint.h>
#include <stddef.h>

/* ── Binary Frame Protocol ──────────────────────────────────
 *  与文本协议共存。首字节 0xBF 表示二进制帧，否则回退文本。
 *
 *  请求帧:
 *    [0xBF] [opcode] [len_hi] [len_lo] [payload...]
 *    字节0:  魔数 0xBF
 *    字节1:  操作码
 *    字节2-3: 负载长度 (大端 uint16)
 *    字节4+:  负载 (可选，根据 opcode)
 *
 *  响应帧:
 *    [0xBF] [opcode] [status_hi] [status_lo] [len_hi] [len_lo] [payload...]
 *    字节0:    魔数 0xBF
 *    字节1:    操作码 (0x00=错误)
 *    字节2-3:  状态码 (0=成功)
 *    字节4-5:  负载长度
 *    字节6+:   负载 (JSON 或文本)
 * ───────────────────────────────────────────────────────── */

#define BF_MAGIC        0xBF
#define BF_HEADER_LEN   4   /* magic + opcode + len(2) */
#define BF_RESP_HEADER  6   /* magic + opcode + status(2) + len(2) */
#define BF_MAX_PAYLOAD  2048
#define BF_BUF_SIZE     (BF_HEADER_LEN + BF_MAX_PAYLOAD)

/* ── 操作码 ────────────────────────────────────────────── */
enum bf_opcode {
    BF_ERR       = 0x00,   /* 错误响应 */
    BF_PING      = 0x01,   /* ping → pong */
    BF_STATUS    = 0x02,   /* 文本状态报告 */
    BF_STATE     = 0x03,   /* JSON 状态 (Aurora) */
    BF_SOUL      = 0x04,   /* soul hex dump */
    BF_EXEC      = 0x05,   /* 执行命令 */
    BF_QUERY     = 0x06,   /* 自然语言查询 */
    BF_AUDIT     = 0x07,   /* 代码审计 */
    BF_CODEGEN   = 0x08,   /* 代码生成 */
    BF_TASK      = 0x09,   /* 提交任务 */
    BF_RESULT    = 0x0A,   /* 查任务结果 */
    BF_TASKS     = 0x0B,   /* 任务队列状态 */
    BF_MENTOR    = 0x0C,   /* 师徒阶段 */
    BF_DISPATCH  = 0x0D,   /* dispatch 统计 */
    BF_CUSTOM    = 0x0E,   /* 自定义文本命令 */
    BF_RAW       = 0xFF    /* 回退到文本处理 */
};

/* ── 编解码辅助 ────────────────────────────────────────── */

/* 编码请求帧: 返回帧长度，0=错误 */
static inline int bf_encode_req(uint8_t *buf, size_t cap,
                                uint8_t opcode, const void *payload, uint16_t plen) {
    if (!buf || cap < BF_HEADER_LEN + plen || plen > BF_MAX_PAYLOAD)
        return 0;
    buf[0] = BF_MAGIC;
    buf[1] = opcode;
    buf[2] = (plen >> 8) & 0xFF;
    buf[3] = plen & 0xFF;
    if (plen > 0 && payload)
        __builtin_memcpy(buf + BF_HEADER_LEN, payload, plen);
    return BF_HEADER_LEN + plen;
}

/* 解码请求帧: 返回 opcode，payload 指针和长度通过参数返回 */
static inline uint8_t bf_decode_req(const uint8_t *buf, size_t len,
                                    const uint8_t **payload, uint16_t *plen) {
    if (!buf || len < BF_HEADER_LEN || buf[0] != BF_MAGIC)
        return 0;
    *plen = ((uint16_t)buf[2] << 8) | buf[3];
    if (*plen > BF_MAX_PAYLOAD) return 0;
    if (BF_HEADER_LEN + *plen > len) return 0;
    *payload = (*plen > 0) ? buf + BF_HEADER_LEN : NULL;
    return buf[1];
}

/* 编码响应帧 */
static inline int bf_encode_resp(uint8_t *buf, size_t cap,
                                 uint8_t opcode, uint16_t status,
                                 const void *payload, uint16_t plen) {
    if (!buf || cap < BF_RESP_HEADER + plen || plen > BF_MAX_PAYLOAD)
        return 0;
    buf[0] = BF_MAGIC;
    buf[1] = opcode;
    buf[2] = (status >> 8) & 0xFF;
    buf[3] = status & 0xFF;
    buf[4] = (plen >> 8) & 0xFF;
    buf[5] = plen & 0xFF;
    if (plen > 0 && payload)
        __builtin_memcpy(buf + BF_RESP_HEADER, payload, plen);
    return BF_RESP_HEADER + plen;
}

/* 是否为二进制帧 */
static inline int bf_is_binary(const uint8_t *buf, size_t len) {
    return (len > 0 && buf[0] == BF_MAGIC);
}

#endif /* TORKD_BINARY_H */
