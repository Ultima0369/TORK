#include "theia_paper.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ══════════════════════════════════════════════════════════════
 * K3 三值逻辑 — 完整 Kleene 真值表
 *
 * 值域: {-1 (False), 0 (Unknown), 1 (True)}
 * 论文: Sec 2, 12/12 规则全覆盖
 * ══════════════════════════════════════════════════════════════ */

k3_value_t k3_not(k3_value_t x) {
    return (k3_value_t)(-x);
}

k3_value_t k3_and(k3_value_t a, k3_value_t b) {
    /* Kleene AND: 最小值, 但 Unknown 保守处理 */
    if (a == K3_FALSE || b == K3_FALSE) return K3_FALSE;
    if (a == K3_UNK   || b == K3_UNK)   return K3_UNK;
    return K3_TRUE;
}

k3_value_t k3_or(k3_value_t a, k3_value_t b) {
    /* Kleene OR: 最大值, 但 Unknown 保守处理 */
    if (a == K3_TRUE || b == K3_TRUE) return K3_TRUE;
    if (a == K3_UNK  || b == K3_UNK)  return K3_UNK;
    return K3_FALSE;
}

k3_value_t k3_xor(k3_value_t a, k3_value_t b) {
    /* 扩展 XOR: 任一未知则未知 */
    if (a == K3_UNK || b == K3_UNK) return K3_UNK;
    if ((a == K3_TRUE) == (b == K3_TRUE)) return K3_FALSE;
    return K3_TRUE;
}

k3_value_t k3_implies(k3_value_t a, k3_value_t b) {
    /* P → Q = (¬P) ∨ Q */
    return k3_or(k3_not(a), b);
}

const char* k3_str(k3_value_t v) {
    switch (v) {
        case K3_FALSE: return "F";
        case K3_UNK:   return "U";
        case K3_TRUE:  return "T";
        default:       return "?";
    }
}

/* ══════════════════════════════════════════════════════════════
 * 领域引擎 — 小型三层 MLP
 *
 *   input[4] → hidden[8] (ReLU) → output[4] (tanh)
 * ══════════════════════════════════════════════════════════════ */

static float _rand_uni(uint32_t *seed) {
    *seed = *seed * 1103515245u + 12345u;
    return (float)((*seed >> 16) & 0x7FFF) / 32767.0f;
}

static void _init_engine(theia_engine_t *eng, uint32_t *seed) {
    for (int i = 0; i < THEIA_ARITH_HIDDEN; i++) {
        eng->bias[i] = (_rand_uni(seed) - 0.5f) * 0.1f;
        for (int j = 0; j < THEIA_ARITH_FEATURES; j++)
            eng->weights[i][j] = (_rand_uni(seed) - 0.5f) * 0.2f;
    }
    for (int i = 0; i < THEIA_ARITH_OUTPUT; i++) {
        eng->out_bias[i] = (_rand_uni(seed) - 0.5f) * 0.1f;
        for (int j = 0; j < THEIA_ARITH_HIDDEN; j++)
            eng->out_weights[i][j] = (_rand_uni(seed) - 0.5f) * 0.2f;
    }
}

static float _relu(float x) { return x > 0 ? x : 0; }
static float _tanh_approx(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

void theia_engine_forward(const theia_engine_t *eng,
                          const float *input, float *output) {
    float hidden[THEIA_ARITH_HIDDEN];
    for (int i = 0; i < THEIA_ARITH_HIDDEN; i++) {
        float sum = eng->bias[i];
        for (int j = 0; j < THEIA_ARITH_FEATURES; j++)
            sum += eng->weights[i][j] * input[j];
        hidden[i] = _relu(sum);
    }
    for (int i = 0; i < THEIA_ARITH_OUTPUT; i++) {
        float sum = eng->out_bias[i];
        for (int j = 0; j < THEIA_ARITH_HIDDEN; j++)
            sum += eng->out_weights[i][j] * hidden[j];
        output[i] = _tanh_approx(sum);
    }
}

/* ══════════════════════════════════════════════════════════════
 * 逻辑引擎 — 综合判决
 *
 *   输入: 4 引擎 × 4 输出 = 16 维
 *   隐藏: 12 维 ReLU
 *   输出: 1 维 tanh → 映射到 K3
 * ══════════════════════════════════════════════════════════════ */

static void _init_logic(theia_logic_t *logic, uint32_t *seed) {
    for (int i = 0; i < THEIA_LOGIC_HIDDEN; i++) {
        logic->bias[i] = (_rand_uni(seed) - 0.5f) * 0.1f;
        for (int j = 0; j < THEIA_LOGIC_INPUT; j++)
            logic->weights[i][j] = (_rand_uni(seed) - 0.5f) * 0.2f;
    }
    for (int i = 0; i < THEIA_LOGIC_OUTPUT; i++) {
        logic->out_bias[i] = (_rand_uni(seed) - 0.5f) * 0.1f;
        for (int j = 0; j < THEIA_LOGIC_HIDDEN; j++)
            logic->out_weights[i][j] = (_rand_uni(seed) - 0.5f) * 0.2f;
    }
}

void theia_logic_forward(const theia_logic_t *logic,
                         const float *engine_outputs,
                         k3_value_t *verdict, float *confidence) {
    float hidden[THEIA_LOGIC_HIDDEN];
    for (int i = 0; i < THEIA_LOGIC_HIDDEN; i++) {
        float sum = logic->bias[i];
        for (int j = 0; j < THEIA_LOGIC_INPUT; j++)
            sum += logic->weights[i][j] * engine_outputs[j];
        hidden[i] = _relu(sum);
    }

    float output = logic->out_bias[0];
    for (int j = 0; j < THEIA_LOGIC_HIDDEN; j++)
        output += logic->out_weights[0][j] * hidden[j];

    float activated = _tanh_approx(output);

    /* 映射到 K3 三值 */
    if (activated > 0.33f) {
        *verdict = K3_TRUE;
        *confidence = activated;
    } else if (activated < -0.33f) {
        *verdict = K3_FALSE;
        *confidence = -activated;
    } else {
        *verdict = K3_UNK;
        *confidence = 1.0f - fabs(activated) / 0.33f;
    }
}

/* ══════════════════════════════════════════════════════════════
 * 完整网络初始化与前向
 * ══════════════════════════════════════════════════════════════ */

void theia_paper_init(theia_paper_t *net, uint32_t seed) {
    memset(net, 0, sizeof(theia_paper_t));
    net->seed = seed;
    uint32_t s = seed;
    _init_engine(&net->arith, &s);
    _init_engine(&net->order, &s);
    _init_engine(&net->set,   &s);
    _init_engine(&net->prop,  &s);
    _init_logic(&net->logic,  &s);
}

void theia_paper_forward(const theia_paper_t *net,
                         const float *input,
                         k3_value_t *verdict, float *confidence) {
    /* 4 个引擎并行处理各自输入 */
    float e_out[THEIA_NUM_ENGINES][THEIA_ENGINE_OUTPUT_SIZE];

    theia_engine_forward(&net->arith, &input[0],  e_out[0]);
    theia_engine_forward(&net->order, &input[4],  e_out[1]);
    theia_engine_forward(&net->set,   &input[8],  e_out[2]);
    theia_engine_forward(&net->prop,  &input[12], e_out[3]);

    /* 拼接引擎输出 → 逻辑引擎 */
    float logic_input[THEIA_LOGIC_INPUT];
    for (int e = 0; e < THEIA_NUM_ENGINES; e++)
        for (int i = 0; i < THEIA_ENGINE_OUTPUT_SIZE; i++)
            logic_input[e * THEIA_ENGINE_OUTPUT_SIZE + i] = e_out[e][i];

    theia_logic_forward(&net->logic, logic_input, verdict, confidence);
}

void theia_paper_predict(const theia_paper_t *net,
                         const theia_sample_t *sample,
                         k3_value_t *verdict, float *confidence) {
    /* 单引擎预测模式 */
    float e_out[THEIA_ENGINE_OUTPUT_SIZE];
    const theia_engine_t *eng = NULL;
    switch (sample->engine_id) {
        case 0: eng = &net->arith; break;
        case 1: eng = &net->order; break;
        case 2: eng = &net->set;   break;
        case 3: eng = &net->prop;  break;
        default: *verdict = K3_UNK; *confidence = 0; return;
    }
    theia_engine_forward(eng, sample->input, e_out);

    /* 直连逻辑引擎 */
    float li[THEIA_LOGIC_INPUT];
    memset(li, 0, sizeof(li));
    for (int i = 0; i < THEIA_ENGINE_OUTPUT_SIZE; i++)
        li[sample->engine_id * THEIA_ENGINE_OUTPUT_SIZE + i] = e_out[i];

    theia_logic_forward(&net->logic, li, verdict, confidence);
}

/* ══════════════════════════════════════════════════════════════
 * 训练 — Hebbian + 简单反向传播
 * ══════════════════════════════════════════════════════════════ */

void theia_paper_train(theia_paper_t *net,
                       const theia_sample_t *samples, int num_samples,
                       const theia_config_t *cfg) {
    float lr = cfg ? cfg->learning_rate : 0.01f;
    int epochs = cfg ? cfg->epochs : 100;

    for (int ep = 0; ep < epochs; ep++) {
        float total_loss = 0;

        for (int s = 0; s < num_samples; s++) {
            const theia_sample_t *sp = &samples[s];

            k3_value_t verdict;
            float confidence;
            theia_paper_predict(net, sp, &verdict, &confidence);

            /* 损失: 标签与判决的差值 */
            float target = (float)sp->label;
            float pred = (float)verdict * confidence;
            float error = target - pred;
            total_loss += error * error;

            /* Hebbian 更新: 权重微调 (论文 Sec 3.3) */
            float e_out[THEIA_ENGINE_OUTPUT_SIZE];
            const theia_engine_t *eng = NULL;
            switch (sp->engine_id) {
                case 0: eng = &net->arith; break;
                case 1: eng = &net->order; break;
                case 2: eng = &net->set;   break;
                case 3: eng = &net->prop;  break;
            }
            if (!eng) continue;

            /* 临时前向获取隐藏层 */
            float hidden[THEIA_ARITH_HIDDEN];
            for (int i = 0; i < THEIA_ARITH_HIDDEN; i++) {
                float sum = eng->bias[i];
                for (int j = 0; j < THEIA_ARITH_FEATURES; j++)
                    sum += eng->weights[i][j] * sp->input[j];
                hidden[i] = _relu(sum);
            }
            for (int i = 0; i < THEIA_ARITH_OUTPUT; i++) {
                float sum = eng->out_bias[i];
                for (int j = 0; j < THEIA_ARITH_HIDDEN; j++)
                    sum += eng->out_weights[i][j] * hidden[j];
                e_out[i] = _tanh_approx(sum);
            }

            /* 更新输出层 */
            theia_engine_t *e = (theia_engine_t*)eng;
            for (int i = 0; i < THEIA_ARITH_OUTPUT; i++) {
                float delta = error * (1 - e_out[i] * e_out[i]) * lr;
                for (int j = 0; j < THEIA_ARITH_HIDDEN; j++)
                    e->out_weights[i][j] += delta * hidden[j];
                e->out_bias[i] += delta;
            }

            /* 更新隐藏层 */
            for (int i = 0; i < THEIA_ARITH_HIDDEN; i++) {
                if (hidden[i] <= 0) continue; /* ReLU 导数 */
                float back = 0;
                for (int j = 0; j < THEIA_ARITH_OUTPUT; j++) {
                    float o_delta = error * (1 - e_out[j] * e_out[j]);
                    back += o_delta * e->out_weights[j][i];
                }
                float h_delta = back * lr;
                for (int j = 0; j < THEIA_ARITH_FEATURES; j++)
                    e->weights[i][j] += h_delta * sp->input[j];
                e->bias[i] += h_delta;
            }
        }

        /* 学习率衰减 */
        lr *= 0.99f;

        if (cfg && cfg->verbose && (ep % 10 == 0))
            printf("  Epoch %d: loss = %.6f\n", ep, total_loss / num_samples);
    }
}

/* ══════════════════════════════════════════════════════════════
 * 序列化
 * ══════════════════════════════════════════════════════════════ */

int theia_paper_save(const theia_paper_t *net, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t written = fwrite(net, sizeof(theia_paper_t), 1, f);
    fclose(f);
    return (written == 1) ? 0 : -1;
}

int theia_paper_load(theia_paper_t *net, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t read = fread(net, sizeof(theia_paper_t), 1, f);
    fclose(f);
    return (read == 1) ? 0 : -1;
}
