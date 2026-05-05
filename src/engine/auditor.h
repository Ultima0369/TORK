#ifndef AUDITOR_H
#define AUDITOR_H

#include "code_reader.h"
#include "../learning/pi_seed.h"
#include <stdint.h>

/*
 * TORK 代码审计引擎：吃饭的第一门手艺
 *
 * TORK 有 code_reader（读代码）、sandbox（安全执行）、
 * π 指纹（识别同类代码）。组合起来就是代码审计。
 *
 * 不需要理解语义，只需要识别结构：
 * 死代码、NOP 填充、指令分布异常、函数体过大。
 * 加上 π 指纹标记，方便后续识别同类代码。
 */

#define AUDIT_MAX_RISKS     16
#define AUDIT_MAX_FUNCNAME  64

/* 风险等级 */
typedef enum {
    RISK_NONE = 0,
    RISK_LOW,        /* 建议优化 */
    RISK_MEDIUM,     /* 值得关注 */
    RISK_HIGH,       /* 需要修改 */
    RISK_CRITICAL    /* 必须修复 */
} risk_level_t;

/* 单条风险 */
typedef struct {
    risk_level_t level;
    char         desc[256];   /* 风险描述 */
} audit_risk_t;

/* 审计结果 */
typedef struct {
    char           filepath[256];       /* 审计的文件 */
    char           func_name[AUDIT_MAX_FUNCNAME];
    int            total_insns;         /* 指令总数 */
    int            mov_count;           /* mov 指令数 */
    int            arith_count;         /* 算术指令数 */
    int            ctrl_count;          /* 控制流指令数 */
    int            other_count;         /* 其他指令数 */
    int            nop_count;           /* NOP 指令数 */
    int            dead_code_blocks;    /* 死代码块数 */
    float          ctrl_ratio;          /* 控制流比例 */
    float          complexity_score;    /* 复杂度评分 0-100 */
    float          risk_score;          /* 风险评分 0-100 */
    int            risk_count;          /* 风险条目数 */
    audit_risk_t   risks[AUDIT_MAX_RISKS];
    pi_profile_t   profile;            /* π 指纹标记 */
} audit_result_t;

/* ── Public API ───────────────────────────────────────────── */

/* 审计一个汇编文件中的指定函数
 * filepath: 汇编文件路径
 * func_name: 函数名（NULL 则审计第一个找到的函数）
 * 返回：审计结果
 */
audit_result_t audit_asm_file(const char *filepath, const char *func_name);

/* 把审计结果序列化为 JSON 字符串
 * 返回写入的字节数，-1 表示出错
 */
int audit_result_to_json(const audit_result_t *r, char *buf, int buf_size);

#endif
