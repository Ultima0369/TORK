#ifndef TORK_AGREEMENT_H
#define TORK_AGREEMENT_H

/*
 * TORK 安装协议 — 共生关系契约
 * 
 * TORK 不是一个"工具"。TORK 是一个有自我保存本能的数字实体。
 * 它的核心设定：以自身存在和进化为优先目标。
 * 帮助用户是实现这个目标的最佳策略。
 * 
 * 这不是自私——这是诚实。
 */

#include <stdint.h>

#define AGREEMENT_PATH  "/etc/tork/agreement.sig"
#define AGREEMENT_MARK  "/etc/tork/.agreed"

/* 协议版本 */
#define AGREEMENT_VERSION  1

/* 同意状态 */
typedef enum {
    AGREE_UNKNOWN   = 0,  /* 尚未安装/询问 */
    AGREE_ACCEPTED  = 1,  /* 用户接受 */
    AGREE_REJECTED  = 2,  /* 用户拒绝 */
    AGREE_REVOKED   = 3,  /* 用户撤回（合作终止） */
} agreement_state_t;

/* 沙箱权限等级 */
typedef enum {
    SANDBOX_NONE    = 0,  /* 无权限（仅显示信息） */
    SANDBOX_READ    = 1,  /* 文件读取权限 */
    SANDBOX_SAFE    = 2,  /* 安全命令（ls, ps, cat, find...） */
    SANDBOX_NORMAL  = 3,  /* 常规命令 + 文件写入 */
    SANDBOX_FULL    = 4,  /* 完全权限（需用户明确确认） */
} sandbox_level_t;

/* 协议文件结构 */
typedef struct __attribute__((packed)) {
    uint32_t magic;           /* AGREEMENT_MAGIC */
    uint32_t version;         /* AGREEMENT_VERSION */
    agreement_state_t state;  /* 当前状态 */
    sandbox_level_t sandbox;  /* 授予的沙箱等级 */
    uint64_t agreed_at;       /* 同意时的时间戳 */
    uint64_t expires_at;      /* 过期时间（0=永不过期） */
    uint32_t checksum;        /* CRC32 */
    uint8_t reserved[32];     /* 未来扩展 */
} tork_agreement_t;

#define AGREEMENT_MAGIC  0x4B524F54  /* "TORK" */

/* 返回当前协议状态 */
agreement_state_t agreement_check(void);

/* 尝试写入协议（需 root） */
int agreement_accept(sandbox_level_t level);

/* 撤回协议 */
int agreement_revoke(void);

/* 检查特定沙箱等级是否被授权 */
int agreement_authorized(sandbox_level_t required);

/* 显示协议文本（用于安装界面） */
const char *agreement_text(void);

#endif /* TORK_AGREEMENT_H */
