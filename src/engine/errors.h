#ifndef ERRORS_H
#define ERRORS_H

/* ══════════════════════════════════════════════════════════════════
 * TORK Unified Error Code Matrix v1.0
 *
 * 所有模块共用一套错误码。每个模块分配一个错误码段：
 *   模块ID << 16 | 错误码
 *
 * 返回值约定：
 *    >= 0  → 成功（具体含义因函数而异）
 *    < 0   → 错误（用 ER_ 宏解包）
 *
 * 用法：
 *   int ret = some_function();
 *   if (ret < 0) {
 *       int mod_id = ER_MODULE(ret);
 *       int err    = ER_CODE(ret);
 *       printf("[%s] error %d: %s\n", er_module_name(mod_id), err, er_strerror(ret));
 *   }
 * ══════════════════════════════════════════════════════════════════ */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 模块ID ──────────────────────────────────────────────────── */
#define ER_MOD_CORE         0x01    /* tork_core (asm) */
#define ER_MOD_ENGINE       0x02    /* tork_engine */
#define ER_MOD_SCHEDULER    0x03    /* scheduler */
#define ER_MOD_DISPATCH     0x04    /* dispatch */
#define ER_MOD_TLN          0x05    /* TLN */
#define ER_MOD_MONITOR      0x06    /* monitor */
#define ER_MOD_FISSION      0x07    /* fission */
#define ER_MOD_BLACKBOARD   0x08    /* blackboard */
#define ER_MOD_INDUCTOR     0x09    /* inductor */
#define ER_MOD_PERSISTOR    0x0A    /* persistor */
#define ER_MOD_CODEC        0x0B    /* code_reader / code_modifier */
#define ER_MOD_SANDBOX      0x0C    /* sandbox */
#define ER_MOD_SOUL         0x0D    /* soul_access */
#define ER_MOD_TORKD        0x0E    /* torkd */
#define ER_MOD_LEARNING     0x0F    /* learning subsystem */
#define ER_MOD_DISTRIBUTED  0x10    /* distributed / grid */
#define ER_MOD_ROLLBACK     0x11    /* rollback */
#define ER_MOD_AGREEMENT    0x12    /* agreement */
#define ER_MOD_GRID         0x13    /* grid connector */
#define ER_MOD_USER         0x7F    /* reserved */

/* ── 错误码范围 ──────────────────────────────────────────────── */
#define ER_OK               0       /* 成功 */
#define ER_FAIL             -1      /* 通用失败 */

/* 通用错误 (0x01-0x1F, 所有模块共用) */
#define ER_INVARG           -2      /* 无效参数 */
#define ER_NOMEM            -3      /* 内存不足 */
#define ER_IO               -4      /* IO 错误 */
#define ER_AGAIN            -5      /* 资源暂时不可用，请重试 */
#define ER_BUSY             -6      /* 资源忙 */
#define ER_TIMEOUT          -7      /* 超时 */
#define ER_INIT             -8      /* 未初始化 */
#define ER_NOENT            -9      /* 条目不存在 */
#define ER_EXIST            -10     /* 条目已存在 */
#define ER_PERM             -11     /* 权限不足 */
#define ER_CORRUPT          -12     /* 数据损坏 */
#define ER_CRC              -13     /* CRC 校验失败 */
#define ER_ABORTED          -14     /* 操作被取消 */
#define ER_NOTIMPL          -15     /* 未实现 */
#define ER_LIMIT            -16     /* 达到上限 */
#define ER_AGAIN_LATER      -17     /* 当前无法执行，稍后重试 */
#define ER_STALL            -18     /* 检测到失速 */

/* ── 错误码编码/解码宏 ──────────────────────────────────────── */
#define ER_MAKE(mod_id, code)   (-((int)(mod_id) << 16 | (uint16_t)(-(code))))
#define ER_MODULE(err)          ((int)((-(err)) >> 16) & 0xFFFF)
#define ER_CODE(err)            ((int)(-(err)) & 0xFFFF)

/* ── 便捷构造宏 ──────────────────────────────────────────────── */
/* 用法: return ER(ENGINE, INVARG); */
#define ER(mod, code)           ER_MAKE(ER_MOD_##mod, ER_##code)

/* ── 错误码查询 ──────────────────────────────────────────────── */

/* 获取模块名称 */
static inline const char *er_module_name(int mod_id) {
    switch (mod_id) {
    case ER_MOD_CORE:        return "core";
    case ER_MOD_ENGINE:      return "engine";
    case ER_MOD_SCHEDULER:   return "scheduler";
    case ER_MOD_DISPATCH:    return "dispatch";
    case ER_MOD_TLN:         return "tln";
    case ER_MOD_MONITOR:     return "monitor";
    case ER_MOD_FISSION:     return "fission";
    case ER_MOD_BLACKBOARD:  return "blackboard";
    case ER_MOD_INDUCTOR:    return "inductor";
    case ER_MOD_PERSISTOR:   return "persistor";
    case ER_MOD_CODEC:       return "codec";
    case ER_MOD_SANDBOX:     return "sandbox";
    case ER_MOD_SOUL:        return "soul";
    case ER_MOD_TORKD:       return "torkd";
    case ER_MOD_LEARNING:    return "learning";
    case ER_MOD_DISTRIBUTED: return "distributed";
    case ER_MOD_ROLLBACK:    return "rollback";
    case ER_MOD_AGREEMENT:   return "agreement";
    case ER_MOD_GRID:        return "grid";
    case ER_MOD_USER:        return "user";
    default:                 return "unknown";
    }
}

/* 获取错误描述 */
static inline const char *er_code_name(int code) {
    switch (code) {
    case 0:               return "OK";
    case -1:              return "FAIL";
    case ER_INVARG:       return "INVARG";
    case ER_NOMEM:        return "NOMEM";
    case ER_IO:           return "IO";
    case ER_AGAIN:        return "AGAIN";
    case ER_BUSY:         return "BUSY";
    case ER_TIMEOUT:      return "TIMEOUT";
    case ER_INIT:         return "INIT";
    case ER_NOENT:        return "NOENT";
    case ER_EXIST:        return "EXIST";
    case ER_PERM:         return "PERM";
    case ER_CORRUPT:      return "CORRUPT";
    case ER_CRC:          return "CRC";
    case ER_ABORTED:      return "ABORTED";
    case ER_NOTIMPL:      return "NOTIMPL";
    case ER_LIMIT:        return "LIMIT";
    case ER_AGAIN_LATER:  return "AGAIN_LATER";
    case ER_STALL:        return "STALL";
    default:              return "UNKNOWN";
    }
}

/* 获取完整错误描述 */
static inline const char *er_strerror(int err) {
    if (err == 0) return "OK";
    int mod = ER_MODULE(err);
    int code = ER_CODE(err);
    /* 使用静态缓冲区 - 注意非线程安全但在TORK上下文没问题 */
    static char buf[64];
    snprintf(buf, sizeof(buf), "%s/%s",
             er_module_name(mod), er_code_name(code));
    return buf;
}

#ifdef __cplusplus
}
#endif

#endif /* ERRORS_H */
