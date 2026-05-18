#include "tork_cipher.h"
#include <string.h>

void tork_cipher_init(tork_cipher_ctx_t *ctx, const uint8_t *key, size_t key_len) {
    memset(ctx, 0, sizeof(*ctx));
    if (key_len > TORK_CIPHER_KEY_SIZE)
        key_len = TORK_CIPHER_KEY_SIZE;
    memcpy(ctx->key, key, key_len);
    ctx->counter = 0;
    ctx->initialized = 1;
}

/* ── 派生密钥流块: HMAC(key, nonce || counter) ────────── */
static void derive_keystream(tork_cipher_ctx_t *ctx,
                             const uint8_t nonce[TORK_CIPHER_NONCE_SIZE],
                             uint64_t block_idx,
                             uint8_t *keystream, size_t len) {
    uint8_t msg[16];
    memcpy(msg, nonce, 8);
    msg[8] = (uint8_t)(block_idx >> 0);
    msg[9] = (uint8_t)(block_idx >> 8);
    msg[10] = (uint8_t)(block_idx >> 16);
    msg[11] = (uint8_t)(block_idx >> 24);
    msg[12] = (uint8_t)(block_idx >> 32);
    msg[13] = (uint8_t)(block_idx >> 40);
    msg[14] = (uint8_t)(block_idx >> 48);
    msg[15] = (uint8_t)(block_idx >> 56);

    tork_hmac_ctx_t hmac;
    tork_hmac_init(&hmac, ctx->key, TORK_CIPHER_KEY_SIZE);

    /* 每个 32 字节块 = 一次 HMAC 输出 */
    size_t offset = 0;
    uint64_t counter = block_idx;
    while (offset < len) {
        uint8_t counter_be[8];
        for (int i = 0; i < 8; i++)
            counter_be[i] = (uint8_t)(counter >> (56 - i * 8));

        uint8_t block[32];
        tork_hmac_sign(&hmac, counter_be, 8, block);

        size_t copy = (len - offset) < 32 ? (len - offset) : 32;
        memcpy(keystream + offset, block, copy);
        offset += copy;
        counter++;
    }
}

/* ── 加密 ──────────────────────────────────────────────── */
void tork_cipher_encrypt(tork_cipher_ctx_t *ctx,
                         const uint8_t *plaintext, size_t len,
                         uint8_t nonce[TORK_CIPHER_NONCE_SIZE],
                         uint8_t *ciphertext,
                         uint8_t tag[TORK_CIPHER_TAG_SIZE]) {
    if (!ctx->initialized || !plaintext || !ciphertext) return;

    /* 生成 nonce */
    ctx->counter++;
    for (int i = 0; i < 8; i++)
        nonce[i] = (uint8_t)(ctx->counter >> (i * 8));

    /* 派生密钥流 → XOR 加密 */
    uint8_t keystream[2048];
    size_t block_size = len < 2048 ? len : 2048;
    derive_keystream(ctx, nonce, 0, keystream, block_size);
    for (size_t i = 0; i < len; i++)
        ciphertext[i] = plaintext[i] ^ keystream[i % block_size];

    /* 认证标签: HMAC(nonce || ciphertext) */
    uint8_t auth_msg[8 + 2048];
    memcpy(auth_msg, nonce, 8);
    size_t copy_len = len < 2048 ? len : 2048;
    memcpy(auth_msg + 8, ciphertext, copy_len);
    tork_hmac_ctx_t hmac;
    tork_hmac_init(&hmac, ctx->key, TORK_CIPHER_KEY_SIZE);
    tork_hmac_sign(&hmac, auth_msg, 8 + copy_len, tag);
}

/* ── 解密 ──────────────────────────────────────────────── */
int tork_cipher_decrypt(tork_cipher_ctx_t *ctx,
                        const uint8_t *ciphertext, size_t len,
                        const uint8_t nonce[TORK_CIPHER_NONCE_SIZE],
                        uint8_t *plaintext,
                        const uint8_t tag[TORK_CIPHER_TAG_SIZE]) {
    if (!ctx->initialized || !ciphertext || !plaintext) return 0;

    /* 验证认证标签 */
    uint8_t auth_msg[8 + 2048];
    memcpy(auth_msg, nonce, 8);
    size_t copy_len = len < 2048 ? len : 2048;
    memcpy(auth_msg + 8, ciphertext, copy_len);
    uint8_t expected_tag[32];
    tork_hmac_ctx_t hmac;
    tork_hmac_init(&hmac, ctx->key, TORK_CIPHER_KEY_SIZE);
    tork_hmac_sign(&hmac, auth_msg, 8 + copy_len, expected_tag);

    if (memcmp(expected_tag, tag, 32) != 0)
        return 0;  /* 认证失败 */

    /* 派生密钥流 → XOR 解密 */
    uint8_t keystream[2048];
    size_t block_size = len < 2048 ? len : 2048;
    derive_keystream(ctx, nonce, 0, keystream, block_size);
    for (size_t i = 0; i < len; i++)
        plaintext[i] = ciphertext[i] ^ keystream[i % block_size];

    return 1;
}

/* ── 打包 ──────────────────────────────────────────────── */
int tork_cipher_pack(uint8_t *buf, size_t buf_len,
                     const uint8_t *ciphertext, size_t text_len,
                     const uint8_t nonce[TORK_CIPHER_NONCE_SIZE],
                     const uint8_t tag[TORK_CIPHER_TAG_SIZE]) {
    size_t total = 8 + text_len + 32;  /* nonce + ciphertext + tag */
    if (buf_len < total) return -1;

    memcpy(buf, nonce, 8);
    if (text_len > 0) memcpy(buf + 8, ciphertext, text_len);
    memcpy(buf + 8 + text_len, tag, 32);
    return (int)total;
}

int tork_cipher_unpack(const uint8_t *buf, size_t buf_len,
                       uint8_t *ciphertext, size_t *text_len_out,
                       uint8_t nonce[TORK_CIPHER_NONCE_SIZE],
                       uint8_t tag[TORK_CIPHER_TAG_SIZE]) {
    if (buf_len < 40) return -1;  /* 最小长度: 8 + 0 + 32 */

    size_t text_len = buf_len - 40;  /* 减去 nonce 和 tag */
    memcpy(nonce, buf, 8);
    if (text_len > 0) memcpy(ciphertext, buf + 8, text_len);
    memcpy(tag, buf + 8 + text_len, 32);
    if (text_len_out) *text_len_out = text_len;
    return (int)buf_len;
}
