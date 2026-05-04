#ifndef QUERY_H
#define QUERY_H

#include <stdint.h>

/* ── TORK 离线查询引擎 ───────────────────────────────────────
 *  让用户可以直接问 TORK 问题，TORK 用自己的记忆回答。
 *  不需要云端 API，不需要网络。
 *  知道就说知道，不知道就说不知道。
 * ──────────────────────────────────────────────────────────── */

#define QUERY_MAX_RESPONSE 1024

/* ── 查询类型 ──────────────────────────────────────────────── */
typedef enum {
    QUERY_STATUS,       /* "你还好吗？" */
    QUERY_MEMORY,       /* "你记得什么？" */
    QUERY_LEARNED,      /* "你学到了什么？" */
    QUERY_ENVIRONMENT,  /* "我电脑什么配置？" */
    QUERY_PATTERNS,     /* "你发现了什么规律？" */
    QUERY_ADVICE,       /* "我该注意什么？" */
    QUERY_ENERGY,       /* "你现在消耗多少资源？" */
    QUERY_BRANCHES,     /* "你的分支在做什么？" */
    QUERY_HEALTH,       /* "你健康吗？" */
    QUERY_UNKNOWN       /* "我不知道" */
} query_type_t;

/* ── 查询接口 ──────────────────────────────────────────────── */
/* 处理用户查询, 写响应到 response 缓冲区 */
/* soul: 当前 Soul 状态指针 */
/* response: 输出缓冲区 */
/* max_len: 缓冲区大小 */
void query_handle(const char *input, void *soul, char *response, int max_len);

#endif /* QUERY_H */
