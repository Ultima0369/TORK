#ifndef THEIA_H
#define THEIA_H

/* ── THEIA 纯神经网络推理引擎 ──────────────────────────────
 *  轻量前馈神经网络，纯 C，零 malloc，静态数组。
 *  支持 Hebbian 在线学习，输出兼容 TLN 三值逻辑 {-1, 0, +1}。
 * ──────────────────────────────────────────────────────────── */

#include <stdint.h>
#include <stddef.h>

/* ── 架构常量 ────────────────────────────────────────────── */
#define THEIA_MAX_LAYERS    8
#define THEIA_MAX_NEURONS   64
#define THEIA_MAX_INPUTS    32
#define THEIA_MAX_OUTPUTS   16
#define THEIA_NAME_LEN      32

/* ── 激活函数 ────────────────────────────────────────────── */
typedef enum {
    THEIA_ACT_IDENTITY = 0,   /* f(x) = x       (输入层专用) */
    THEIA_ACT_TANH    = 1,    /* f(x) = tanh(x) (隐藏层)      */
    THEIA_ACT_SIGMOID = 2,    /* f(x) = 1/(1+e^-x)           */
    THEIA_ACT_RELU    = 3,    /* f(x) = max(0,x)             */
    THEIA_ACT_TERNARY = 4     /* f(x) = sign(x)  ∈ {-1,0,+1} (输出层) */
} theia_act_t;

/* ── 层描述 ──────────────────────────────────────────────── */
typedef struct {
    uint16_t     num_neurons;       /* 本层神经元数 */
    theia_act_t  activation;        /* 激活函数 */
    float        weights[THEIA_MAX_NEURONS][THEIA_MAX_NEURONS]; /* 权重矩阵 [本层][前层] */
    float        biases[THEIA_MAX_NEURONS];
    float        trace[THEIA_MAX_NEURONS]; /* 活动迹 (用于 STDP) */
} theia_layer_t;

/* ── 网络 ────────────────────────────────────────────────── */
typedef struct {
    char          name[THEIA_NAME_LEN];
    uint8_t       num_layers;
    uint16_t      num_inputs;       /* 输入维度 */
    uint16_t      num_outputs;      /* 输出维度 */
    theia_layer_t layers[THEIA_MAX_LAYERS];

    /* 运行状态 */
    float         input_buf[THEIA_MAX_INPUTS];
    float         output_buf[THEIA_MAX_OUTPUTS];
    int8_t        tln_output[THEIA_MAX_OUTPUTS]; /* 三值量化输出 */
    uint64_t      inference_count;
    uint64_t      learn_count;

    /* 学习率 */
    float         lr;               /* 全局学习率 (默认 0.01) */
    float         stdp_tau;         /* STDP 时间常数 (默认 0.1) */
} theia_net_t;

/* ── 训练样本 ────────────────────────────────────────────── */
typedef struct {
    float input[THEIA_MAX_INPUTS];
    float target[THEIA_MAX_OUTPUTS];
} theia_sample_t;

/* ── API ──────────────────────────────────────────────────── */

/* 初始化一个前馈网络 */
void theia_init(theia_net_t *net, const char *name,
                uint16_t num_inputs, uint16_t num_outputs);

/* 添加层 (返回层索引, -1 失败) */
int theia_add_layer(theia_net_t *net, uint16_t num_neurons, theia_act_t act);

/* 随机初始化所有权重 [-1, 1] */
void theia_randomize(theia_net_t *net, int seed);

/* 前向推理: 输入 → 输出 */
void theia_forward(theia_net_t *net, const float *input);

/* 获取浮点输出 */
const float *theia_output(const theia_net_t *net);

/* 获取三值量化输出 {-1, 0, +1} */
const int8_t *theia_tln_output(const theia_net_t *net);

/* 在线学习 Hebbian + STDP: 根据目标调整权重 */
void theia_learn(theia_net_t *net, const float *target, float reward);

/* 单步训练: forward + learn */
void theia_train_step(theia_net_t *net, const theia_sample_t *sample);

/* 序列化: 权重 → 字节数组 */
size_t theia_serialize(const theia_net_t *net, uint8_t *buf, size_t buf_len);

/* 反序列化: 字节数组 → 权重 */
int theia_deserialize(theia_net_t *net, const uint8_t *buf, size_t buf_len);

/* 保存/加载 (通过文件系统) */
int theia_save(const theia_net_t *net, const char *path);
int theia_load(theia_net_t *net, const char *path);

/* 打印网络拓扑 */
void theia_print(const theia_net_t *net);

#endif /* THEIA_H */
