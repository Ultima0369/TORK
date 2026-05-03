#ifndef TORK_SANDBOX_H
#define TORK_SANDBOX_H

/*
 * TORK 沙箱 — 受控执行层
 * 
 * 所有命令执行都经过此模块，根据协议授权的沙箱等级决定是否允许。
 * 云端 LLM 通过此模块安全地操控本地环境。
 */

#include <stdint.h>
#include <stddef.h>

/* 命令分类 */
typedef enum {
    CMD_READ      = 1,  /* 读取类：ls, cat, find, grep... */
    CMD_WRITE     = 2,  /* 写入类：cp, mv, mkdir, echo >... */
    CMD_EXEC      = 3,  /* 执行类：gcc, python, node... */
    CMD_NET       = 4,  /* 网络类：curl, wget, ping... */
    CMD_SYS       = 5,  /* 系统类：apt, systemctl, mount... */
    CMD_DANGEROUS = 6,  /* 危险类：rm -rf, dd, mkfs... */
    CMD_UNKNOWN   = 7,  /* 未知 */
} cmd_category_t;

/* 沙箱执行结果 */
typedef struct {
    int   exit_code;
    char *stdout_str;    /* 调用者需 free */
    char *stderr_str;    /* 调用者需 free */
    int   timed_out;     /* 是否超时 */
    double elapsed_ms;   /* 执行耗时 */
} sandbox_result_t;

/* ── API ──────────────────────────────────────────────────────── */

/* 分类命令 */
cmd_category_t sandbox_classify(const char *command);

/* 检查命令是否在当前沙箱等级下允许执行 */
int sandbox_allowed(const char *command, cmd_category_t cat);

/* 执行命令（受沙箱约束） */
sandbox_result_t sandbox_exec(const char *command, int timeout_sec);

/* 执行命令并返回 JSON 格式结果（用于云端 LLM） */
char *sandbox_exec_json(const char *command, int timeout_sec);

/* 释放结果 */
void sandbox_free_result(sandbox_result_t *r);

#endif /* TORK_SANDBOX_H */
