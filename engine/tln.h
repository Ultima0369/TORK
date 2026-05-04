#ifndef TLN_H
#define TLN_H

#include <stdint.h>
#include <stddef.h>

/* ── TORK 三进制逻辑网络 (Ternary Logic Network) ────────────────
 *
 * 每个神经元输入/输出/权重都是三态: -1, 0, +1
 * 没有浮点，没有激活函数，只有整数加法和钳位。
 * 带反馈的时序推理回路——和 L1 心跳同构。
 *
 * 输出语义:
 *   +1  →  确定执行
 *   -1  →  确定拒绝
 *    0  →  悬置，等下一 tick 再判断
 * ──────────────────────────────────────────────────────────────── */

#define TLN_INPUTS   16
#define TLN_HIDDEN   32
#define TLN_OUTPUTS   8

typedef int8_t tln_val_t;  /* -1, 0, +1 */

typedef struct {
    tln_val_t w_ih[TLN_INPUTS * TLN_HIDDEN];   /* 输入→隐藏 */
    tln_val_t w_hh[TLN_HIDDEN * TLN_HIDDEN];   /* 隐藏→隐藏 (自环) */
    tln_val_t w_ho[TLN_HIDDEN * TLN_OUTPUTS];  /* 隐藏→输出 */
    tln_val_t state[TLN_HIDDEN];                /* 上一 tick 隐藏状态 */
    tln_val_t output[TLN_OUTPUTS];              /* 最新输出 */
    uint32_t ticks;                             /* 推理 tick 计数 */
    uint32_t mutation_count;                    /* 累计变异次数 */
} TernaryNet;

/* 初始化: 零权重 (全悬置态) */
void tln_init(TernaryNet *net);

/* 单步推理: 输入 Soul 特征，输出决策向量
 * 每次调用推进一个 tick，状态自环 */
void tln_step(TernaryNet *net,
              const tln_val_t input[TLN_INPUTS],
              tln_val_t output[TLN_OUTPUTS]);

/* 从 Soul 状态提取 TLN 输入 (Soul → 三值编码) */
void tln_encode_soul(const uint8_t *soul_buf,
                     uint8_t hw_stress, int8_t drive,
                     uint16_t gen_count,
                     const tln_val_t pattern_out[4],
                     tln_val_t input[TLN_INPUTS]);

/* 解码输出: 8 个三值输出 → 调度器可用的决策参数 */
void tln_decode_output(const tln_val_t output[TLN_OUTPUTS],
                       int *action_hint,    /* +1=激进, -1=保守, 0=悬置 */
                       int *modify_hint,    /* +1=可变异, -1=禁变异, 0=悬置 */
                       int *explore_hint,   /* +1=探索, -1=收敛, 0=悬置 */
                       int *energy_hint);   /* +1=高功率, -1=省电, 0=悬置 */

/* 进化: 对权重做三值变异 (概率 p, 0.0~1.0)
 * 返回实际变异数 */
int tln_mutate(TernaryNet *net, float p);

/* 克隆网络 */
void tln_clone(const TernaryNet *src, TernaryNet *dst);

/* 比较: 0=相同, >0=差异数 */
int tln_diff(const TernaryNet *a, const TernaryNet *b);

/* 持久化 */
int tln_save(const TernaryNet *net, const char *path);
int tln_load(TernaryNet *net, const char *path);

/* 调试输出 */
void tln_print_state(const TernaryNet *net);
void tln_print_output(const tln_val_t output[TLN_OUTPUTS]);

/* 权重总量: 1792 个三值 = 448 字节 (2bit/weight) */

#endif /* TLN_H */