#ifndef THEIA_PAPER_H
#define THEIA_PAPER_H

/* ══════════════════════════════════════════════════════════════
 * THEIA: 论文架构复刻 — arXiv:2604.11284v2
 *
 * "THEIA: Learning Complete Kleene Three-Valued Logic
 *  in a Pure-Neural Modular Architecture"
 *  作者: Augustus Haoyang Li
 *
 * 核心创新:
 *   1. 4 领域引擎 (算术/序关系/集合/命题逻辑)
 *   2. 逻辑引擎收敛判决
 *   3. 三值 Kleene K3 逻辑 {-1, 0, +1}
 *   4. 模块化 → 泛化到 500 步
 *   5. "延迟判决" — 上游不提交最终值
 * ══════════════════════════════════════════════════════════════ */

#include <stdint.h>
#include <stddef.h>
#include <math.h>

/* ── K3 三值逻辑常量 ──────────────────────────────────── */
typedef int8_t k3_value_t;

#define K3_FALSE  (-1)  /* False */
#define K3_UNK     0    /* Unknown (第三值) */
#define K3_TRUE    1    /* True */

/* ── 领域引擎尺寸 ──────────────────────────────────────── */
#define THEIA_ARITH_FEATURES  4   /* a, b, op, result */
#define THEIA_ARITH_HIDDEN    8
#define THEIA_ARITH_OUTPUT    4

#define THEIA_ORDER_FEATURES  4   /* a, b, rel, result */
#define THEIA_ORDER_HIDDEN    8
#define THEIA_ORDER_OUTPUT    4

#define THEIA_SET_FEATURES    4   /* elem, set_id, rel, result */
#define THEIA_SET_HIDDEN      8
#define THEIA_SET_OUTPUT      4

#define THEIA_PROP_FEATURES   4   /* p, q, op, result */
#define THEIA_PROP_HIDDEN     8
#define THEIA_PROP_OUTPUT     4

#define THEIA_NUM_ENGINES     4
#define THEIA_ENGINE_OUTPUT_SIZE 4  /* 各引擎输出维度一致 */

#define THEIA_LOGIC_INPUT     (THEIA_NUM_ENGINES * THEIA_ENGINE_OUTPUT_SIZE)
#define THEIA_LOGIC_HIDDEN    12
#define THEIA_LOGIC_OUTPUT    1

#define THEIA_TOTAL_INPUT_SIZE    16  /* 4 engines × 4 features */
#define THEIA_TOTAL_PARAMS        2752  /* ~2.75K (论文 2.75M 的缩比实现) */

/* ── 领域引擎 (小 MLP) ────────────────────────────────── */
typedef struct {
    float weights[THEIA_ARITH_HIDDEN][THEIA_ARITH_FEATURES];
    float bias[THEIA_ARITH_HIDDEN];
    float out_weights[THEIA_ARITH_OUTPUT][THEIA_ARITH_HIDDEN];
    float out_bias[THEIA_ARITH_OUTPUT];
} theia_engine_t;

/* ── 逻辑引擎 (收敛模块) ─────────────────────────────── */
typedef struct {
    float weights[THEIA_LOGIC_HIDDEN][THEIA_LOGIC_INPUT];
    float bias[THEIA_LOGIC_HIDDEN];
    float out_weights[THEIA_LOGIC_OUTPUT][THEIA_LOGIC_HIDDEN];
    float out_bias[THEIA_LOGIC_OUTPUT];
} theia_logic_t;

/* ── 完整 THEIA 网络 ────────────────────────────────── */
typedef struct {
    theia_engine_t arith;    /* 算术引擎 */
    theia_engine_t order;    /* 序关系引擎 */
    theia_engine_t set;      /* 集合引擎 */
    theia_engine_t prop;     /* 命题逻辑引擎 */
    theia_logic_t  logic;    /* 逻辑收敛引擎 */
    uint32_t       seed;     /* 随机种子 */
} theia_paper_t;

/* ── 训练样本 ──────────────────────────────────────────── */
typedef struct {
    int    engine_id;   /* 0=arith, 1=order, 2=set, 3=prop */
    float  input[4];    /* 引擎输入 */
    k3_value_t label;   /* 标签: K3_TRUE/K3_FALSE/K3_UNK */
} theia_sample_t;

/* ── 训练配置 ──────────────────────────────────────────── */
typedef struct {
    float  learning_rate;
    int    epochs;
    int    batch_size;
    int    verbose;
} theia_config_t;

/* ══════════════════════════════════════════════════════════════
 * 公共 API
 * ══════════════════════════════════════════════════════════════ */

/* K3 逻辑运算 */
k3_value_t k3_not(k3_value_t x);
k3_value_t k3_and(k3_value_t a, k3_value_t b);
k3_value_t k3_or(k3_value_t a, k3_value_t b);
k3_value_t k3_xor(k3_value_t a, k3_value_t b);
k3_value_t k3_implies(k3_value_t a, k3_value_t b);
const char* k3_str(k3_value_t v);

/* 初始化 */
void theia_paper_init(theia_paper_t *net, uint32_t seed);

/* 领域引擎前向 */
void theia_engine_forward(const theia_engine_t *eng,
                          const float *input, float *output);

/* 逻辑引擎前向 */
void theia_logic_forward(const theia_logic_t *logic,
                         const float *engine_outputs,
                         k3_value_t *verdict, float *confidence);

/* 完整前向传播 */
void theia_paper_forward(const theia_paper_t *net,
                         const float *input,
                         k3_value_t *verdict, float *confidence);

/* 单样本预测 */
void theia_paper_predict(const theia_paper_t *net,
                         const theia_sample_t *sample,
                         k3_value_t *verdict, float *confidence);

/* 训练 */
void theia_paper_train(theia_paper_t *net,
                       const theia_sample_t *samples, int num_samples,
                       const theia_config_t *cfg);

/* 序列化 */
int theia_paper_save(const theia_paper_t *net, const char *path);
int theia_paper_load(theia_paper_t *net, const char *path);

#endif /* THEIA_PAPER_H */
