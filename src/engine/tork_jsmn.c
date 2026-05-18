/* ══════════════════════════════════════════════════════════════
 * JSMN — JSON 小到极致 (嵌入式实现)
 *
 * 原始代码: https://github.com/zserge/jsmn
 * 许可证: MIT
 *
 * 为 TORK 做了精简:
 *   - 移除宽字符支持 (不需要)
 *   - 添加便捷查找/提取工具
 *   - 添加类型安全的输出函数
 * ══════════════════════════════════════════════════════════════ */

#include "tork_jsmn.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* ── 跳过空白 ──────────────────────────────────────────── */
static const char *jsmn_skip_ws(const char *js, jsmn_parser *p) {
    while (js[p->pos] != '\0' && (js[p->pos] == ' ' || js[p->pos] == '\t' ||
           js[p->pos] == '\n' || js[p->pos] == '\r'))
        p->pos++;
    return &js[p->pos];
}

/* ── 解析字符串 ────────────────────────────────────────── */
static int jsmn_parse_string(jsmn_parser *p, const char *js,
                             jsmntok_t *tokens, unsigned int num_tokens) {
    if (js[p->pos] != '"') return JSMN_ERROR_INVAL;

    if (tokens && p->toknext < num_tokens) {
        tokens[p->toknext].start = p->pos;
        tokens[p->toknext].type = JSMN_STRING;
    }

    /* 跳过开头的 '"' */
    p->pos++;

    while (js[p->pos] != '\0') {
        if (js[p->pos] == '"') {
            p->pos++;
            if (tokens && p->toknext < num_tokens)
                tokens[p->toknext].end = p->pos;
            p->toknext++;
            return 0;
        }
        if (js[p->pos] == '\\') {
            p->pos++;
            if (js[p->pos] == '\0') return JSMN_ERROR_PART;
        }
        p->pos++;
    }
    return JSMN_ERROR_PART;
}

/* ── 解析基本值 ────────────────────────────────────────── */
static int jsmn_parse_primitive(jsmn_parser *p, const char *js,
                                jsmntok_t *tokens, unsigned int num_tokens) {
    if (tokens && p->toknext < num_tokens) {
        tokens[p->toknext].start = p->pos;
        tokens[p->toknext].type = JSMN_PRIMITIVE;
    }

    while (js[p->pos] != '\0') {
        switch (js[p->pos]) {
            case ' ': case '\t': case '\n': case '\r':
            case ',': case ']': case '}':
                if (tokens && p->toknext < num_tokens)
                    tokens[p->toknext].end = p->pos;
                p->toknext++;
                return 0;
        }
        p->pos++;
    }

    if (tokens && p->toknext < num_tokens)
        tokens[p->toknext].end = p->pos;
    p->toknext++;
    return 0;
}

/* ── 主解析函数 ────────────────────────────────────────── */
int jsmn_parse(jsmn_parser *p, const char *js,
               jsmntok_t *tokens, unsigned int num_tokens) {
    int r;
    int i;
    int token;

    /* 确保第一个字符是有效的 JSON */
    jsmn_skip_ws(p, js);
    if (js[p->pos] == '\0') return JSMN_ERROR_INVAL;

    /* 循环直到 JSON 被完全解析或栈空 */
    for (;;) {
        jsmn_skip_ws(p, js);
        if (js[p->pos] == '\0') {
            /* 检查是否所有括号都闭合了 */
            for (i = p->toknext - 1; i >= 0; i--) {
                if (tokens && tokens[i].type != JSMN_STRING) {
                    /* 栈上有未闭合的 object/array */
                    if (tokens[i].size == 0)
                        return JSMN_ERROR_PART;
                }
            }
            return p->toknext;
        }

        token = p->toknext;

        /* 分配令牌 */
        if (tokens && token >= num_tokens) {
            /* 但继续解析以确定真正的令牌数 */
        }

        switch (js[p->pos]) {
            case '{':
                p->pos++;
                if (tokens && token < num_tokens) {
                    tokens[token].type = JSMN_OBJECT;
                    tokens[token].start = p->pos - 1;
                    tokens[token].end = 0;
                    tokens[token].size = 0;
                    tokens[token].tksuper = p->tksuper;
                }
                p->tksuper = token;
                p->toknext++;
                break;

            case '}':
                p->pos++;
                if (tokens && token < num_tokens) {
                    tokens[token].end = p->pos;
                    tokens[token].size++;
                    /* 回到父级 */
                    if (p->tksuper >= 0 && tokens) {
                        int super = tokens[token].tksuper;
                        if (super >= 0 && (int)p->toknext <= super + 1)
                            tokens[super].size++;
                    }
                }
                /* 出栈 */
                if (p->tksuper >= 0 && tokens) {
                    p->tksuper = tokens[p->tksuper].tksuper;
                }
                break;

            case '[':
                p->pos++;
                if (tokens && token < num_tokens) {
                    tokens[token].type = JSMN_ARRAY;
                    tokens[token].start = p->pos - 1;
                    tokens[token].end = 0;
                    tokens[token].size = 0;
                    tokens[token].tksuper = p->tksuper;
                }
                p->tksuper = token;
                p->toknext++;
                break;

            case ']':
                p->pos++;
                if (tokens && token < num_tokens) {
                    tokens[token].end = p->pos;
                    tokens[token].size++;
                    if (p->tksuper >= 0 && tokens) {
                        int super = tokens[token].tksuper;
                        if (super >= 0 && (int)p->toknext <= super + 1)
                            tokens[super].size++;
                    }
                }
                if (p->tksuper >= 0 && tokens) {
                    p->tksuper = tokens[p->tksuper].tksuper;
                }
                break;

            case '\"':
                r = jsmn_parse_string(p, js, tokens, num_tokens);
                if (r < 0) return r;
                /* 如果是对象中的值，父 object 计数 */
                if (p->tksuper >= 0 && tokens) {
                    int super = p->tksuper;
                    tokens[super].size++;
                }
                break;

            case '\0':
                return p->toknext;

            default:
                r = jsmn_parse_primitive(p, js, tokens, num_tokens);
                if (r < 0) return r;
                if (p->tksuper >= 0 && tokens) {
                    int super = p->tksuper;
                    tokens[super].size++;
                }
                break;
        }
    }
}

/* ── 便捷查找 ──────────────────────────────────────────── */
int jsmn_find_key(const jsmntok_t *tokens, int count,
                  const char *js, const char *key) {
    if (!tokens || count < 2 || !js || !key) return -1;

    /* 顶层必须是 object */
    if (tokens[0].type != JSMN_OBJECT) return -1;

    /* 遍历 object 的所有 key-value 对 */
    int i = 1; /* 第一个子令牌 */
    int end = tokens[0].end;

    /* 使用 size 来知道有多少个 key-value 对 */
    for (int j = 0; j < tokens[0].size * 2 && i < count; j += 2, i += 2) {
        if (i >= count) break;
        if (tokens[i].type == JSMN_STRING) {
            int klen = tokens[i].end - tokens[i].start - 2;
            if (klen > 0) {
                const char *kstart = js + tokens[i].start + 1;
                if (strncmp(kstart, key, klen) == 0 && (size_t)klen == strlen(key)) {
                    /* key 匹配, 返回 value 索引 */
                    if (i + 1 < count)
                        return i + 1;
                }
            }
        }
    }
    return -1;
}

/* ── 提取字符串 ────────────────────────────────────────── */
int jsmn_get_string(const jsmntok_t *tokens, int idx,
                    const char *js, char *buf, size_t buf_size) {
    if (!tokens || idx < 0 || !js || !buf || buf_size < 1)
        return -1;
    if (tokens[idx].type != JSMN_STRING)
        return -2;

    int start = tokens[idx].start + 1; /* 跳过 '"' */
    int end = tokens[idx].end - 1;     /* 跳过 '"' */
    int len = end - start;

    if (len < 0) return -3;
    if ((size_t)len >= buf_size) len = (int)(buf_size - 1);

    strncpy(buf, js + start, len);
    buf[len] = '\0';
    return len;
}

/* ── 提取整数 ──────────────────────────────────────────── */
int jsmn_get_int(const jsmntok_t *tokens, int idx,
                 const char *js, int *out) {
    if (!tokens || idx < 0 || !js || !out)
        return -1;
    if (tokens[idx].type != JSMN_PRIMITIVE)
        return -2;

    int len = tokens[idx].end - tokens[idx].start;
    char buf[32];
    if (len >= 32) return -3;
    strncpy(buf, js + tokens[idx].start, len);
    buf[len] = '\0';
    *out = atoi(buf);
    return 0;
}

/* ── 提取浮点 ──────────────────────────────────────────── */
int jsmn_get_double(const jsmntok_t *tokens, int idx,
                    const char *js, double *out) {
    if (!tokens || idx < 0 || !js || !out)
        return -1;
    if (tokens[idx].type != JSMN_PRIMITIVE)
        return -2;

    int len = tokens[idx].end - tokens[idx].start;
    char buf[64];
    if (len >= 64) return -3;
    strncpy(buf, js + tokens[idx].start, len);
    buf[len] = '\0';
    *out = strtod(buf, NULL);
    return 0;
}

/* ── 令牌 → 字符串 ────────────────────────────────────── */
int jsmn_token_to_str(const jsmntok_t *tok, const char *js,
                      char *buf, size_t buf_size) {
    if (!tok || !js || !buf || buf_size < 2) return -1;

    int len = tok->end - tok->start;
    if ((size_t)len >= buf_size) len = (int)(buf_size - 1);

    strncpy(buf, js + tok->start, len);
    buf[len] = '\0';
    return len;
}

/* ── 初始化 ────────────────────────────────────────────── */
void jsmn_init(jsmn_parser *p) {
    p->pos = 0;
    p->toknext = 0;
    p->tksuper = -1;
}
