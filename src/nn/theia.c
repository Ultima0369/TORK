#include "theia.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── 激活函数实现 ────────────────────────────────────────── */
static inline float _act_identity(float x) { return x; }
static inline float _act_tanh(float x) {
    if (x > 10.0f) return 1.0f;
    if (x < -10.0f) return -1.0f;
    float e2x = expf(2.0f * x);
    return (e2x - 1.0f) / (e2x + 1.0f);
}
static inline float _act_sigmoid(float x) {
    if (x > 10.0f) return 1.0f;
    if (x < -10.0f) return 0.0f;
    return 1.0f / (1.0f + expf(-x));
}
static inline float _act_relu(float x) {
    return x > 0.0f ? x : 0.0f;
}
static inline int8_t _act_ternary(float x) {
    if (x > 0.3f) return 1;
    if (x < -0.3f) return -1;
    return 0;
}

static float (*_act_funcs[])(float) = {
    _act_identity, _act_tanh, _act_sigmoid, _act_relu, NULL
};

static const char *_act_names[] = {
    "identity", "tanh", "sigmoid", "relu", "ternary"
};

/* ── 网络初始化 ──────────────────────────────────────────── */
void theia_init(theia_net_t *net, const char *name,
                uint16_t num_inputs, uint16_t num_outputs) {
    memset(net, 0, sizeof(theia_net_t));
    if (name) {
        strncpy(net->name, name, THEIA_NAME_LEN - 1);
        net->name[THEIA_NAME_LEN - 1] = '\0';
    }
    net->num_inputs = num_inputs < THEIA_MAX_INPUTS ? num_inputs : THEIA_MAX_INPUTS;
    net->num_outputs = num_outputs < THEIA_MAX_OUTPUTS ? num_outputs : THEIA_MAX_OUTPUTS;
    net->lr = 0.01f;
    net->stdp_tau = 0.1f;
    net->num_layers = 0;
}

/* ── 添加层 ──────────────────────────────────────────────── */
int theia_add_layer(theia_net_t *net, uint16_t num_neurons, theia_act_t act) {
    if (net->num_layers >= THEIA_MAX_LAYERS) return -1;
    if (num_neurons > THEIA_MAX_NEURONS) return -1;
    int idx = net->num_layers++;
    net->layers[idx].num_neurons = num_neurons;
    net->layers[idx].activation = act;
    /* 权重要等所有层添加完毕后统一初始化 */
    return idx;
}

/* ── 随机初始化 ──────────────────────────────────────────── */
void theia_randomize(theia_net_t *net, int seed) {
    srand(seed);
    uint16_t prev_neurons = net->num_inputs;

    for (int l = 0; l < net->num_layers; l++) {
        theia_layer_t *layer = &net->layers[l];
        uint16_t n = layer->num_neurons;

        /* 实际前一层神经元数: 如果是第一层，输入层宽度 */
        uint16_t actual_prev = (l == 0) ? net->num_inputs :
                               net->layers[l - 1].num_neurons;

        for (uint16_t i = 0; i < n; i++) {
            layer->biases[i] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
            layer->trace[i] = 0.0f;
            for (uint16_t j = 0; j < actual_prev; j++) {
                layer->weights[i][j] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
            }
        }
        prev_neurons = n;
    }
}

/* ── 前向推理 ────────────────────────────────────────────── */
void theia_forward(theia_net_t *net, const float *input) {
    /* 复制输入 */
    for (uint16_t i = 0; i < net->num_inputs; i++) {
        net->input_buf[i] = input[i];
    }

    float *cur_input = net->input_buf;
    uint16_t cur_dim = net->num_inputs;

    for (int l = 0; l < net->num_layers; l++) {
        theia_layer_t *layer = &net->layers[l];
        uint16_t n = layer->num_neurons;
        float (*act_fn)(float) = _act_funcs[layer->activation];

        for (uint16_t i = 0; i < n; i++) {
            float sum = layer->biases[i];
            for (uint16_t j = 0; j < cur_dim; j++) {
                sum += layer->weights[i][j] * cur_input[j];
            }
            float out = act_fn ? act_fn(sum) : sum;

            /* 如果是输出层 (最后一层), 写入 output_buf */
            if (l == net->num_layers - 1) {
                if (i < net->num_outputs) {
                    net->output_buf[i] = out;
                    net->tln_output[i] = _act_ternary(out);
                }
            }

            /* 为下一层准备输入 */
            if (l + 1 < net->num_layers) {
                /* 把当前层输出写入下一层的 "临时输入" */
                /* 我们用一个小技巧: 把输出写回 input_buf, 因为每次覆盖 */
                if (l == 0) {
                    /* 第一层输出写回 input_buf (从末尾往前放) */
                    /* 更好的做法: 直接用 layer 的 trace 作为中间缓存 */
                }
            }
        }

        /* 将当前层输出作为下一层输入: 用 trace 字段暂存 */
        for (uint16_t i = 0; i < n; i++) {
            float sum = layer->biases[i];
            for (uint16_t j = 0; j < cur_dim; j++) {
                sum += layer->weights[i][j] * cur_input[j];
            }
            layer->trace[i] = _act_funcs[layer->activation] ?
                              _act_funcs[layer->activation](sum) : sum;
        }

        cur_input = layer->trace;
        cur_dim = n;
    }

    net->inference_count++;
}

/* ── 获取输出 ────────────────────────────────────────────── */
const float *theia_output(const theia_net_t *net) {
    return net->output_buf;
}

const int8_t *theia_tln_output(const theia_net_t *net) {
    return net->tln_output;
}

/* ── Hebbian + STDP 在线学习 ──────────────────────────────── */
void theia_learn(theia_net_t *net, const float *target, float reward) {
    if (net->num_layers == 0) return;

    float effective_lr = net->lr * (reward > 0.0f ? reward : 0.1f);
    if (effective_lr < 0.0001f) effective_lr = 0.0001f;

    uint16_t prev_dim = net->num_inputs;
    float *prev_act = net->input_buf;

    for (int l = 0; l < net->num_layers; l++) {
        theia_layer_t *layer = &net->layers[l];
        uint16_t n = layer->num_neurons;
        int is_output = (l == net->num_layers - 1);

        for (uint16_t i = 0; i < n; i++) {
            float error;
            if (is_output && i < net->num_outputs) {
                error = target[i] - layer->trace[i];
            } else {
                /* 隐藏层: 用迹作为自我调整信号 */
                error = (layer->trace[i] > 0.5f || layer->trace[i] < -0.5f) ?
                         layer->trace[i] * 0.1f : 0.0f;
            }

            /* Hebbian: Δw = lr * error * pre_act */
            for (uint16_t j = 0; j < prev_dim; j++) {
                float hebb = effective_lr * error * prev_act[j];
                /* STDP 调制: 用迹 */
                float stdp = net->stdp_tau * layer->trace[i] * prev_act[j];
                layer->weights[i][j] += hebb + stdp;

                /* 权重裁剪 [-3, 3] 防止发散 */
                if (layer->weights[i][j] > 3.0f) layer->weights[i][j] = 3.0f;
                if (layer->weights[i][j] < -3.0f) layer->weights[i][j] = -3.0f;
            }

            layer->biases[i] += effective_lr * error * 0.1f;
            if (layer->biases[i] > 3.0f) layer->biases[i] = 3.0f;
            if (layer->biases[i] < -3.0f) layer->biases[i] = -3.0f;

            /* 更新活动迹 */
            layer->trace[i] = layer->trace[i] * (1.0f - net->stdp_tau) +
                              error * net->stdp_tau;
        }

        prev_dim = n;
        prev_act = layer->trace;
    }

    net->learn_count++;
}

/* ── 单步训练 ────────────────────────────────────────────── */
void theia_train_step(theia_net_t *net, const theia_sample_t *sample) {
    theia_forward(net, sample->input);
    theia_learn(net, sample->target, 1.0f);
}

/* ── 序列化 ────────────────────────────────────────────── */
size_t theia_serialize(const theia_net_t *net, uint8_t *buf, size_t buf_len) {
    size_t pos = 0;
    uint32_t nl = net->num_layers;

    if (pos + sizeof(nl) > buf_len) return 0;
    memcpy(buf + pos, &nl, sizeof(nl));
    pos += sizeof(nl);

    for (int l = 0; l < nl; l++) {
        const theia_layer_t *layer = &net->layers[l];
        uint16_t nn = layer->num_neurons;
        uint8_t act = (uint8_t)layer->activation;

        if (pos + sizeof(nn) + sizeof(act) > buf_len) return 0;
        memcpy(buf + pos, &nn, sizeof(nn)); pos += sizeof(nn);
        memcpy(buf + pos, &act, sizeof(act)); pos += sizeof(act);

        /* 前一层神经元数 */
        uint16_t prev_n = (l == 0) ? net->num_inputs :
                          net->layers[l - 1].num_neurons;

        size_t wsize = (size_t)nn * prev_n * sizeof(float);
        size_t bsize = (size_t)nn * sizeof(float);
        size_t tsize = (size_t)nn * sizeof(float);

        if (pos + wsize + bsize + tsize > buf_len) return 0;
        memcpy(buf + pos, layer->weights, wsize); pos += wsize;
        memcpy(buf + pos, layer->biases, bsize); pos += bsize;
        memcpy(buf + pos, layer->trace, tsize); pos += tsize;
    }

    return pos;
}

int theia_deserialize(theia_net_t *net, const uint8_t *buf, size_t buf_len) {
    size_t pos = 0;
    uint32_t nl;

    if (pos + sizeof(nl) > buf_len) return -1;
    memcpy(&nl, buf + pos, sizeof(nl)); pos += sizeof(nl);

    if (nl > THEIA_MAX_LAYERS) return -1;
    net->num_layers = (uint8_t)nl;

    for (uint32_t l = 0; l < nl; l++) {
        theia_layer_t *layer = &net->layers[l];
        uint16_t nn; uint8_t act;

        if (pos + sizeof(nn) + sizeof(act) > buf_len) return -1;
        memcpy(&nn, buf + pos, sizeof(nn)); pos += sizeof(nn);
        memcpy(&act, buf + pos, sizeof(act)); pos += sizeof(act);

        layer->num_neurons = nn;
        layer->activation = (theia_act_t)act;

        uint16_t prev_n = (l == 0) ? net->num_inputs :
                          net->layers[l - 1].num_neurons;

        size_t wsize = (size_t)nn * prev_n * sizeof(float);
        size_t bsize = (size_t)nn * sizeof(float);
        size_t tsize = (size_t)nn * sizeof(float);

        if (pos + wsize + bsize + tsize > buf_len) return -1;
        memcpy(layer->weights, buf + pos, wsize); pos += wsize;
        memcpy(layer->biases, buf + pos, bsize); pos += bsize;
        memcpy(layer->trace, buf + pos, tsize); pos += tsize;
    }

    return 0;
}

/* ── 文件保存/加载 ──────────────────────────────────────── */
int theia_save(const theia_net_t *net, const char *path) {
    uint8_t buf[65536];
    size_t len = theia_serialize(net, buf, sizeof(buf));
    if (len == 0) return -1;

    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t written = fwrite(buf, 1, len, f);
    fclose(f);
    return (written == len) ? 0 : -1;
}

int theia_load(theia_net_t *net, const char *path) {
    uint8_t buf[65536];
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t len = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (len == 0) return -1;
    return theia_deserialize(net, buf, len);
}

/* ── 打印 ────────────────────────────────────────────────── */
void theia_print(const theia_net_t *net) {
    printf("THEIA Net: %s\n", net->name);
    printf("  Inputs:  %u  Outputs: %u\n", net->num_inputs, net->num_outputs);
    printf("  Layers:  %u\n", net->num_layers);
    printf("  Lr:      %f  STDP tau: %f\n", net->lr, net->stdp_tau);
    printf("  Inf:     %lu  Learn: %lu\n",
           (unsigned long)net->inference_count,
           (unsigned long)net->learn_count);
    for (int l = 0; l < net->num_layers; l++) {
        const theia_layer_t *layer = &net->layers[l];
        printf("  Layer %d: %u neurons, %s\n",
               l, layer->num_neurons,
               _act_names[layer->activation < 5 ? layer->activation : 0]);
    }
}
