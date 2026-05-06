#ifndef FRACTAL_H
#define FRACTAL_H

#include <stdint.h>
#include <stddef.h>

/*
 * TORK 分形基元 — 比较·差异·模糊·类似
 *
 * 这是TORK唯一的认知操作。从心跳到认知，从个体到群体，
 * 所有智能层都在不同尺度上递归调用同一个闭环。
 *
 * 四步：
 *   1. compare  — 在输入张量上计算距离/差异度量
 *   2. differ   — 提取差异向量（哪些维度偏离，偏离多少）
 *   3. tolerate — 对差异施加 Michaelis-Menten 容忍核
 *                 tolerate(d, t) = d / (d + t)
 *                 不一次判死，允许模糊
 *   4. resemble — 从容忍后的差异中归纳类似模式，输出决策权重
 *
 * 调用者提供维度数、输入向量、参考向量、容忍阈值。
 * 基元返回：差异强度、决策权重、匹配的类似模式索引。
 *
 * 防御：所有入口校验空指针和 dims<=0，NaN/Inf 视为感知污染跳过
 */

/* 分形基元输入 */
typedef struct {
    int      dims;           /* 向量维度 (必须 > 0) */
    float   *input;          /* 当前观测向量 [dims] (不可为 NULL) */
    float   *reference;      /* 参考基准向量 [dims] (不可为 NULL) */
    float   *tolerance;      /* 每维容忍阈值 [dims] (0=不容忍, NULL不可) */
    float   *weights;        /* 每维重要性权重 [dims] (NULL不可, 负值视为0) */
} fractal_input_t;

/* 分形基元输出 */
typedef struct {
    float    delta;          /* 加权差异强度 [0, ∞) */
    float    confidence;     /* 容忍后置信度 [0, 1] */
    int      best_match;     /* 最类似模式索引 (-1=无匹配) */
    float    best_similarity;/* 最类似模式的相似度 [0, 1] */
} fractal_output_t;

/*
 * fractal_step — 执行一次分形基元闭环
 *
 * compare:  逐维计算 |input[i] - reference[i]|
 * differ:   加权求和 → delta
 * tolerate: 对每维差异施加 Michaelis-Menten 容忍核
 *           tolerate(d, t) = d / (d + t)   当 t>0
 *                          = 1.0           当 d>0 且 t==0
 *                          = 0.0           当 d<=0 或 NaN/Inf
 * resemble: 在外部模式库中查找最类似模式（由调用者提供回调）
 *
 * inp:       输入 (NULL → 返回零输出)
 * match:     模式匹配回调 (NULL → 跳过 resemble 步)
 * match_ctx: 回调上下文
 *
 * 返回：fractal_output_t
 */
typedef int (*fractal_match_fn)(const float *vec, int dims,
                                 float *similarity, void *ctx);

fractal_output_t fractal_step(const fractal_input_t *inp,
                               fractal_match_fn match,
                               void *match_ctx);

/*
 * fractal_compare — 纯比较+差异，不触发模式匹配
 * 用于心跳层等不需要归纳类似的场景
 * 输入校验：指针为空或 dims<=0 返回 0.0
 */
float fractal_compare(const float *input, const float *reference,
                       const float *weights, int dims);

/*
 * fractal_tolerate — 单值 Michaelis-Menten 容忍核
 * d=差异值, t=容忍阈值
 * 返回 [0, 1]，0=完全一致，1=完全偏离
 * NaN/Inf 输入返回 0（感知污染不传播）
 */
float fractal_tolerate(float d, float t);

/*
 * fractal_resemble — 在模式库中查找最类似模式
 * vec=待匹配向量, patterns=模式库, n=模式数, dims=维度
 * similarity: 写入相似度 (可为 NULL)
 * 返回最类似模式索引 (-1=无匹配或输入无效)
 */
int fractal_resemble(const float *vec, int dims,
                      const float *patterns, int n,
                      float *similarity);

#endif /* FRACTAL_H */
