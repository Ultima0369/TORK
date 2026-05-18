#ifndef TORK_BITNET_H
#define TORK_BITNET_H

/* ── TORK ↔ BitNet b1.58 三值大脑桥接器 ───────────────────
 *  让 TORK 的 TLN 三值逻辑网络调用本地 BitNet LLM。
 *  两个三值系统 {-1, 0, +1} 通过 HTTP 直连，无需云端。
 *
 *  BitNet 是微软开源的 1-bit LLM 推理框架:
 *    https://github.com/microsoft/BitNet
 *  权重只有三个值 {-1, 0, +1}，与 TORK 的 TLN 同构。
 *
 *  TORK 的 TLN 做微秒级模式识别和快速推理，
 *  BitNet 做百毫秒级语义理解和生成，
 *  两者互补，构成完整的三值智能双核。
 * ──────────────────────────────────────────────────────────── */

#include <stdint.h>
#include <stddef.h>

/* ── 常量 ────────────────────────────────────────────────── */
#define TORK_BITNET_MAX_PROMPT   4096
#define TORK_BITNET_MAX_RESPONSE 8192
#define TORK_BITNET_MAX_MODEL    256
#define TORK_BITNET_DEFAULT_PORT 8080
#define TORK_BITNET_DEFAULT_HOST "127.0.0.1"

/* ── BitNet 状态 ────────────────────────────────────────── */
typedef enum {
    TORK_BITNET_STOPPED = 0,    /* 未启动 */
    TORK_BITNET_STARTING,       /* 启动中 */
    TORK_BITNET_RUNNING,        /* 运行中，可查询 */
    TORK_BITNET_ERROR           /* 出错 */
} tork_bitnet_state_t;

/* ── 查询结果 ────────────────────────────────────────────── */
typedef struct {
    char   response[TORK_BITNET_MAX_RESPONSE];
    int    response_len;
    int    success;              /* 1=成功, 0=失败 */
    float  elapsed_sec;          /* 耗时 */
    int    tokens_generated;     /* 生成的 token 数 */
    char   error[128];           /* 错误信息 */
} tork_bitnet_result_t;

/* ── 桥接器 ────────────────────────────────────────────── */
typedef struct {
    tork_bitnet_state_t state;
    char   host[64];
    int    port;
    char   model_path[TORK_BITNET_MAX_MODEL];
    int    server_pid;           /* BitNet 服务进程 PID */
    int    connect_attempts;     /* 连接尝试次数 */
    int    total_queries;        /* 总查询次数 */
    int    total_tokens;         /* 总生成 token 数 */
} tork_bitnet_bridge_t;

/* ── API ──────────────────────────────────────────────────── */

/* 初始化桥接器（不启动服务） */
void tork_bitnet_init(tork_bitnet_bridge_t *bridge, const char *model_path);

/* 启动 BitNet 服务进程（非阻塞，轮询等待就绪） */
int tork_bitnet_start(tork_bitnet_bridge_t *bridge, int port);

/* 检查服务是否就绪 */
int tork_bitnet_is_ready(tork_bitnet_bridge_t *bridge);

/* 发送提示词，等待完整响应 */
int tork_bitnet_query(tork_bitnet_bridge_t *bridge,
                      const char *prompt,
                      tork_bitnet_result_t *result);

/* 发送提示词，流式回调（每生成一个 token 调用一次 callback） */
typedef void (*tork_bitnet_token_cb)(const char *token, void *userdata);
int tork_bitnet_query_stream(tork_bitnet_bridge_t *bridge,
                             const char *prompt,
                             tork_bitnet_token_cb callback,
                             void *userdata);

/* 停止 BitNet 服务进程 */
void tork_bitnet_stop(tork_bitnet_bridge_t *bridge);

/* 获取当前状态摘要 */
void tork_bitnet_summary(const tork_bitnet_bridge_t *bridge,
                         char *buf, int bufsize);

#endif /* TORK_BITNET_H */
