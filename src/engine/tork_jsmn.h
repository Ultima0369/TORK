/* ══════════════════════════════════════════════════════════════
 * TORK JSON 解析器 — JSMN 嵌入版
 *
 * JSMN (JSON 小到极致) 是一个零依赖的 C JSON 解析器。
 * 整个解析器一个 .c + .h，约 400 行。
 * 无 malloc，无递归，纯迭代。
 *
 * 在 TORK 中用于:
 *   - 解析 SP bridge 传入的 JSON 消息
 *   - 解析配置文件
 *   - 输出结构化日志 (JSON Lines)
 *
 * JSMN 许可证: MIT (兼容 GPLv3)
 * ══════════════════════════════════════════════════════════════ */

#ifndef TORK_JSMN_H
#define TORK_JSMN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── JSMN 令牌类型 ─────────────────────────────────────── */
typedef enum {
    JSMN_UNDEFINED = 0,
    JSMN_OBJECT    = 1,   /* { } */
    JSMN_ARRAY     = 2,   /* [ ] */
    JSMN_STRING    = 3,   /* "..." */
    JSMN_PRIMITIVE = 4,   /* number / boolean / null */
} jsmntype_t;

/* ── JSMN 令牌 ──────────────────────────────────────────── */
typedef struct {
    jsmntype_t type;       /* 令牌类型 */
    int        start;      /* 在 JSON 字符串中的起始位置 */
    int        end;        /* 在 JSON 字符串中的结束位置 */
    int        size;       /* 子元素数量 (object/array) */
} jsmntok_t;

/* ── JSMN 解析器 ────────────────────────────────────────── */
typedef struct {
    unsigned int pos;      /* 当前解析位置 */
    unsigned int toknext;  /* 下一个令牌索引 */
    int          toksuper; /* 父令牌索引 */
} jsmn_parser;

/* ── 初始化解析器 ──────────────────────────────────────── */
void jsmn_init(jsmn_parser *parser);

/* ── 解析 JSON ────────────────────────────────────────────
 * parser    : 解析器实例 (已调用 jsmn_init)
 * js        : JSON 字符串 (null-terminated)
 * tokens    : 令牌数组 (输出)
 * num_tokens: 令牌数组容量
 * 返回值: 解析成功的令牌数 (>0), 或负值错误码:
 *   JSMN_ERROR_NOMEM  = -1  令牌数组空间不足
 *   JSMN_ERROR_INVAL  = -2  无效输入
 *   JSMN_ERROR_PART   = -3  不完整的 JSON
 */
#define JSMN_ERROR_NOMEM  (-1)
#define JSMN_ERROR_INVAL  (-2)
#define JSMN_ERROR_PART   (-3)

int jsmn_parse(jsmn_parser *parser, const char *js,
               jsmntok_t *tokens, unsigned int num_tokens);

/* ── 便捷工具 ──────────────────────────────────────────── */

/* 在 object 中查找 key: 返回 value 令牌索引, -1=未找到 */
int jsmn_find_key(const jsmntok_t *tokens, int count,
                  const char *js, const char *key);

/* 提取字符串值到缓冲区 */
int jsmn_get_string(const jsmntok_t *tokens, int idx,
                    const char *js, char *buf, size_t buf_size);

/* 提取整数值 */
int jsmn_get_int(const jsmntok_t *tokens, int idx,
                 const char *js, int *out);

/* 提取浮点值 */
int jsmn_get_double(const jsmntok_t *tokens, int idx,
                    const char *js, double *out);

/* 令牌 → JSON 字符串 (用于调试) */
int jsmn_token_to_str(const jsmntok_t *tok, const char *js,
                      char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* TORK_JSMN_H */
