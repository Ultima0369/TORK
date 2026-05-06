#ifndef DISPATCH_H
#define DISPATCH_H

/*
 * TORK 统一工具调度层
 *
 * 所有行为经 dispatch 发出 → 执行 → 自动记录 experience → 返回结果。
 * 闭环的胶水：torkd exec、task tick、idler mcts_modify 都走这条路。
 * 借鉴 Claude Code 的 Tool Loop — 统一入口，统一回流。
 */

#include <stdint.h>

/* ── Action 类型 ──────────────────────────────────────────────── */

typedef enum {
    /* 内部行为 */
    DISP_SELF_MODIFY     = 0,   /* 代码自修改 (je→jz等) */
    DISP_SELF_OPTIMIZE   = 1,   /* 死代码/NOP删除 */
    DISP_SELF_MCTS_MOD   = 2,   /* MCTS 决策的代码修改 */
    DISP_SELF_FISSION    = 3,   /* 分裂 */

    /* 外部任务 — 吃饭的手艺 */
    DISP_EXEC_CMD        = 4,   /* 执行外部命令 */
    DISP_ANALYZE_ASM     = 5,   /* 分析汇编文件 */
    DISP_AUDIT_CODE      = 6,   /* 代码安全审计 */

    /* 代码生成 — codegen 管道 */
    DISP_CODEGEN_SEARCH  = 7,   /* MCTS 搜索最优代码变体 */
    DISP_CODEGEN_COMPILE = 8,   /* 编译验证变体 */
    DISP_CODEGEN_BENCH   = 9,   /* benchmark 变体 */

    DISP_NUM_ACTIONS     = 10
} dispatch_action_t;

/* ── Dispatch 输入 ────────────────────────────────────────────── */

typedef struct {
    dispatch_action_t  action;       /* 要执行什么 */
    const char        *input;        /* 命令/文件路径/模板名 */
    const char        *func_name;    /* 目标函数名(可选) */
    const char        *work_dir;     /* 工作目录(可选) */
    int                timeout_sec;  /* 超时秒数 */
    int                iterations;   /* benchmark迭代次数(可选) */
    /* 上下文 — 写入 experience 时需要 */
    uint32_t           tick;         /* 当前 tick */
    uint8_t            hw_stress;    /* 当前硬件压力 */
    int8_t             drive;        /* 当前驱力 */
    uint32_t           gen_count;    /* 世代计数 */
} dispatch_input_t;

/* ── Dispatch 输出 ────────────────────────────────────────────── */

typedef struct {
    int             rc;             /* 0=成功, -1=失败, 403=权限拒绝 */
    int             exit_code;      /* 原始退出码 (exec) */
    char            output[8192];   /* 结果输出 (JSON/text) */
    int             output_len;     /* 输出长度 */
    float           score;          /* 综合得分 (codegen/audit) */
    int             benchmark_ns;   /* benchmark 纳秒 (codegen) */
    int             compile_ok;     /* 编译通过 (codegen) */
    int8_t          exp_outcome;    /* 经验 outcome 值 (自动计算) */
    uint32_t        exp_id;         /* 写入的 experience ID */
} dispatch_output_t;

/* ── Public API ──────────────────────────────────────────────── */

/* 初始化 dispatch 层 */
void dispatch_init(void);

/* 统一调度入口：
 * 执行 action → 记录 experience → 返回结果
 * 这是闭环的关键：所有行为经此发出，所有结果经此回流
 */
dispatch_output_t tork_dispatch(const dispatch_input_t *in);
void dispatch_get_stats(uint32_t *total, uint32_t *success, uint32_t *fail);

/* 获取 action 名称 */
const char *dispatch_action_name(dispatch_action_t action);

/* 统计 */
uint32_t dispatch_total_calls(void);
uint32_t dispatch_total_success(void);
uint32_t dispatch_total_fail(void);

/* 清理 */
void dispatch_cleanup(void);

#endif /* DISPATCH_H */