#include "fractal.h"
#include <math.h>
#include <string.h>

/* ── 单值容忍核 (Michaelis-Menten) ──────────────────────
 * tolerate(d, t) = d / (d + t)   当 t > 0
 *                = 1.0            当 d > 0 且 t == 0
 *                = 0.0            当 d <= 0 或 d 为 NaN/Inf
 *
 * 物理含义：差异 d 在容忍 t 的背景下有多"严重"
 * t 越大，同样的 d 被视为越不严重
 * NaN/Inf 视为感知污染，返回 0（不传播）
 * ──────────────────────────────────────────────────────── */
float fractal_tolerate(float d, float t) {
    if (!isfinite(d) || d <= 0.0f) return 0.0f;
    if (!isfinite(t) || t <= 0.0f) return 1.0f;
    return d / (d + t);
}

/* ── 纯比较+差异 ────────────────────────────────────────
 * 逐维加权绝对差之和
 * 输入校验：指针为空或 dims<=0 返回 0.0
 * ──────────────────────────────────────────────────────── */
float fractal_compare(const float *input, const float *reference,
                       const float *weights, int dims) {
    if (!input || !reference || !weights || dims <= 0)
        return 0.0f;
    float delta = 0.0f;
    for (int i = 0; i < dims; i++) {
        float d = input[i] - reference[i];
        if (!isfinite(d)) continue;  /* 感知污染：跳过该维 */
        if (d < 0.0f) d = -d;
        float w = weights[i];
        if (!isfinite(w) || w < 0.0f) w = 0.0f;
        delta += d * w;
    }
    return delta;
}

/* ── 模式库最类似查找 ────────────────────────────────────
 * 余弦相似度，返回最类似模式索引
 * ──────────────────────────────────────────────────────── */
int fractal_resemble(const float *vec, int dims,
                      const float *patterns, int n,
                      float *similarity) {
    if (similarity) *similarity = 0.0f;
    if (!vec || !patterns || dims <= 0 || n <= 0)
        return -1;

    int best = -1;
    float best_sim = 0.0f;

    /* 预算 vec 的模 */
    float vec_norm = 0.0f;
    for (int i = 0; i < dims; i++) {
        if (!isfinite(vec[i])) continue;
        vec_norm += vec[i] * vec[i];
    }
    vec_norm = sqrtf(vec_norm);
    if (vec_norm < 1e-9f) return -1;

    for (int p = 0; p < n; p++) {
        const float *pat = patterns + p * dims;
        float dot = 0.0f, pat_norm = 0.0f;
        for (int i = 0; i < dims; i++) {
            if (!isfinite(vec[i]) || !isfinite(pat[i])) continue;
            dot += vec[i] * pat[i];
            pat_norm += pat[i] * pat[i];
        }
        pat_norm = sqrtf(pat_norm);
        if (pat_norm < 1e-9f) continue;
        float sim = dot / (vec_norm * pat_norm);
        if (sim > best_sim) {
            best_sim = sim;
            best = p;
        }
    }

    if (similarity) *similarity = best_sim;
    return best;
}

/* ── 分形基元闭环 ────────────────────────────────────────
 * compare → differ → tolerate → resemble
 *
 * 输入校验：inp 为空或指针为空或 dims<=0，返回零输出
 * NaN/Inf 在每维被跳过（感知污染不传播）
 * ──────────────────────────────────────────────────────── */
fractal_output_t fractal_step(const fractal_input_t *inp,
                               fractal_match_fn match,
                               void *match_ctx) {
    fractal_output_t out;
    memset(&out, 0, sizeof(out));

    if (!inp || inp->dims <= 0 ||
        !inp->input || !inp->reference || !inp->tolerance || !inp->weights)
        return out;

    /* 单次遍历：compare + differ + tolerate + 累计权重 */
    float delta = 0.0f;
    float tolerated_sum = 0.0f;
    float total_weight = 0.0f;

    for (int i = 0; i < inp->dims; i++) {
        float d = inp->input[i] - inp->reference[i];
        if (!isfinite(d)) continue;  /* 感知污染：跳过 */
        if (d < 0.0f) d = -d;

        float w = inp->weights[i];
        if (!isfinite(w) || w < 0.0f) w = 0.0f;  /* 负权重无意义 */

        delta += d * w;
        tolerated_sum += fractal_tolerate(d, inp->tolerance[i]) * w;
        total_weight += w;
    }
    out.delta = delta;

    /* 容忍后置信度：差异越小，置信度越高 */
    if (total_weight > 0.0f)
        out.confidence = 1.0f - tolerated_sum / total_weight;
    if (out.confidence < 0.0f) out.confidence = 0.0f;
    if (out.confidence > 1.0f) out.confidence = 1.0f;

    /* Step 4: resemble — 在外部模式库中查找最类似模式 */
    if (match) {
        float sim = 0.0f;
        int idx = match(inp->input, inp->dims, &sim, match_ctx);
        out.best_match = idx;
        out.best_similarity = sim;
    }

    return out;
}
