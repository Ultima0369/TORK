#ifndef CODEGEN_H
#define CODEGEN_H

/*
 * TORK 代码生成管道
 *
 * 需求 → 模板选择 → MCTS变异 → 编译验证 → benchmark → 模式记录
 * TORK 的吃饭手艺：给需求，出代码。
 */

#include <stdint.h>

#define CODEGEN_MAX_VARIANTS    32
#define CODEGEN_MAX_TEMPLATE   16
#define CODEGEN_MAX_SRC        4096
#define CODEGEN_MAX_NAME       64

/* 变异策略 */
typedef enum {
    VAR_NONE = 0,
    VAR_REG_SWAP,       /* 寄存器交换 (rax↔rcx 等) */
    VAR_UNROLL_2,       /* 循环展开 ×2 */
    VAR_UNROLL_4,       /* 循环展开 ×4 */
    VAR_ALIGN_CHANGE,   /* 对齐策略变更 (.p2align N) */
    VAR_BRANCH_HINT,    /* 分支预测提示 */
    VAR_MOVZX_OPT,      /* addq $1 → incq 指令缩短优化 */
    VAR_NOP_PAD,        /* NOP 填充对齐 */
    VAR_NUM_STRATEGIES
} variant_strategy_t;

/* 代码变体 */
typedef struct {
    char            name[CODEGEN_MAX_NAME];   /* 变体名称 */
    char            src[CODEGEN_MAX_SRC];     /* 生成的汇编源码 */
    int             src_len;                  /* 源码长度 */
    variant_strategy_t strategy;              /* 使用的变异策略 */
    int             compile_ok;               /* 编译是否通过 */
    int             benchmark_ns;             /* benchmark 耗时 (纳秒) */
    float           score;                    /* 综合得分 */
} codegen_variant_t;

/* 模板 */
typedef struct {
    char            name[CODEGEN_MAX_NAME];   /* 如 "memcpy_byte_loop" */
    char            src[CODEGEN_MAX_SRC];     /* 骨架汇编 */
    int             src_len;
    int             num_params;               /* 可变参数个数 */
} codegen_template_t;

/* ── Public API ───────────────────────────────────────────── */

/* 初始化代码生成引擎（注册内置模板） */
void codegen_init(void);

/* 注册一个代码模板 */
int codegen_register_template(const char *name, const char *asm_src);

/* 查找模板 */
const codegen_template_t *codegen_find_template(const char *name);

/* 对模板应用变异策略，生成新变体 */
int codegen_apply_variant(const codegen_template_t *tmpl,
                           variant_strategy_t strategy,
                           codegen_variant_t *out);

/* 编译验证变体 (as -o ...) */
int codegen_compile_verify(codegen_variant_t *variant, const char *work_dir);

/* 对变体运行 benchmark */
int codegen_benchmark(codegen_variant_t *variant, const char *work_dir,
                       int iterations, int size);

/* MCTS 驱动的代码生成：选择模板 → 搜索最优变异 → 返回最佳变体 */
int codegen_mcts_search(const char *template_name,
                          codegen_variant_t *best_out,
                          int time_budget_ms);

/* 获取已注册模板数量 */
int codegen_template_count(void);

/* 清理 */
void codegen_cleanup(void);

#endif /* CODEGEN_H */
