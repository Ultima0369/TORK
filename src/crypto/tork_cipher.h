#ifndef TORK_CIPHER_H
#define TORK_CIPHER_H

/* ══════════════════════════════════════════════════════════════
 * TORK 轻量加密层 — HMAC-based stream cipher + auth
 *
 * 用于 SP 桥接传输加密。不是标准 TLS，但在嵌入式场景中提供：
 *   - 机密性 (XOR stream cipher, HMAC-SHA256 派生密钥流)
 *   - 完整性 (HMAC-SHA256 认证标签)
 *   - 防重放 (nonce + counter)
 *
 * 安全等级: 128-bit (SHA256 碰撞抵抗下限)
 * ══════════════════════════════════════════════════════════════ */

#include <stdint.h>
#include <stddef.h>
#include "tork_sha256.h"

#define TORK_CIPHER_KEY_SIZE   32
#define TORK_CIPHER_NONCE_SIZE 8
#define TORK_CIPHER_TAG_SIZE   32  /* HMAC-SHA256 full output */

#ifdef __cplusplus
extern "C" {
#endif

/* ── 加密上下文 ────────────────────────────────────────── */
typedef struct {
    uint8_t key[TORK_CIPHER_KEY_SIZE];    /* 预共享密钥 */
    uint64_t counter;                      /* 加密计数器 */
    int initialized;
} tork_cipher_ctx_t;

/* ── 初始化加密器 (使用预共享密钥) ─────────────────────── */
void tork_cipher_init(tork_cipher_ctx_t *ctx, const uint8_t *key, size_t key_len);

/* ── 加密: plaintext → ciphertext + tag
 *   nonce: 8 字节唯一值 (发送方生成)
 *   tag:   32 字节 HMAC 认证标签
 *   输出 ciphertext 长度 = plaintext_len */
void tork_cipher_encrypt(tork_cipher_ctx_t *ctx,
                         const uint8_t *plaintext, size_t len,
                         uint8_t nonce[TORK_CIPHER_NONCE_SIZE],
                         uint8_t *ciphertext,
                         uint8_t tag[TORK_CIPHER_TAG_SIZE]);

/* ── 解密: ciphertext → plaintext + 验证 tag
 *   返回 1=验证通过, 0=验证失败 */
int tork_cipher_decrypt(tork_cipher_ctx_t *ctx,
                        const uint8_t *ciphertext, size_t len,
                        const uint8_t nonce[TORK_CIPHER_NONCE_SIZE],
                        uint8_t *plaintext,
                        const uint8_t tag[TORK_CIPHER_TAG_SIZE]);

/* ── 帧格式: 用于网络传输的打包/解包
 *   [nonce:8][ciphertext:len][tag:32]
 *   返回打包后的总长度, 或 -1 (buffer 不足) */
int tork_cipher_pack(uint8_t *buf, size_t buf_len,
                     const uint8_t *ciphertext, size_t text_len,
                     const uint8_t nonce[TORK_CIPHER_NONCE_SIZE],
                     const uint8_t tag[TORK_CIPHER_TAG_SIZE]);
int tork_cipher_unpack(const uint8_t *buf, size_t buf_len,
                       uint8_t *ciphertext, size_t *text_len_out,
                       uint8_t nonce[TORK_CIPHER_NONCE_SIZE],
                       uint8_t tag[TORK_CIPHER_TAG_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* TORK_CIPHER_H */
