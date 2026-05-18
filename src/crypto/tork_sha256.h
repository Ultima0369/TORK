#ifndef TORK_SHA256_H
#define TORK_SHA256_H

/* ══════════════════════════════════════════════════════════════
 * TORK SHA256 — 紧凑实现，FIPS 180-4 标准
 * 适用于嵌入式环境，无动态内存分配
 * ══════════════════════════════════════════════════════════════ */

#include <stdint.h>
#include <stddef.h>

#define TORK_SHA256_BLOCK_SIZE  64
#define TORK_SHA256_DIGEST_SIZE 32
#define TORK_HMAC_KEY_SIZE      32
#define TORK_HMAC_SIG_SIZE      32

/* ── SHA256 上下文 ─────────────────────────────────────── */
typedef struct {
    uint8_t  data[TORK_SHA256_BLOCK_SIZE];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];  /* H0-H7 */
} tork_sha256_ctx_t;

/* ── HMAC 上下文 ────────────────────────────────────────── */
typedef struct {
    uint8_t key[TORK_HMAC_KEY_SIZE];
} tork_hmac_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

/* ── SHA256 API ─────────────────────────────────────────── */
void tork_sha256_init(tork_sha256_ctx_t *ctx);
void tork_sha256_update(tork_sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void tork_sha256_final(tork_sha256_ctx_t *ctx, uint8_t hash[TORK_SHA256_DIGEST_SIZE]);
void tork_sha256(const uint8_t *data, size_t len, uint8_t hash[TORK_SHA256_DIGEST_SIZE]);

/* ── HMAC-SHA256 API ────────────────────────────────────── */
void tork_hmac_init(tork_hmac_ctx_t *ctx, const uint8_t *key, size_t key_len);
void tork_hmac_sign(tork_hmac_ctx_t *ctx,
                    const uint8_t *msg, size_t msg_len,
                    uint8_t sig[TORK_HMAC_SIG_SIZE]);
int  tork_hmac_verify(tork_hmac_ctx_t *ctx,
                      const uint8_t *msg, size_t msg_len,
                      const uint8_t sig[TORK_HMAC_SIG_SIZE]);
/* 返回 1=匹配, 0=不匹配 */

#ifdef __cplusplus
}
#endif

#endif /* TORK_SHA256_H */
