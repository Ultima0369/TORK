#include "tln.h"
#include "soul_access.h"
#include "../learning/pi_seed.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/* ── 三值钳位 ────────────────────────────────────────────── */
static inline tln_val_t tln_clamp(int sum) {
    return (sum > 0) ? 1 : (sum < 0) ? -1 : 0;
}

/* ── 初始化 ──────────────────────────────────────────────── */
void tln_init(TernaryNet *net) {
    memset(net, 0, sizeof(*net));
    /* 全零权重 = 全悬置态，网络不做任何判断 */
    /* state 初始化为 0 (悬置) */
    net->ticks = 0;
    net->mutation_count = 0;
    net->observe_ticks = 0;
    net->observe_snapshots = 0;
}

/* ── 单步推理 ──────────────────────────────────────────────
 * 核心计算: 整数加法 + 钳位，无浮点，无乘法器
 * 每次调用推进一个 tick
 */
void tln_step(TernaryNet *net,
              const tln_val_t input[TLN_INPUTS],
              tln_val_t output[TLN_OUTPUTS]) {
    tln_val_t hidden[TLN_HIDDEN];

    /* 输入→隐藏 + 自环反馈 */
    for (int j = 0; j < TLN_HIDDEN; j++) {
        int sum = 0;
        /* 外部输入 */
        for (int i = 0; i < TLN_INPUTS; i++)
            sum += (int)net->w_ih[j * TLN_INPUTS + i] * (int)input[i];
        /* 上一 tick 的隐藏状态回接 (时序推理的核心) */
        for (int k = 0; k < TLN_HIDDEN; k++)
            sum += (int)net->w_hh[j * TLN_HIDDEN + k] * (int)net->state[k];
        hidden[j] = tln_clamp(sum);
    }

    /* 隐藏→输出 */
    for (int j = 0; j < TLN_OUTPUTS; j++) {
        int sum = 0;
        for (int k = 0; k < TLN_HIDDEN; k++)
            sum += (int)net->w_ho[j * TLN_HIDDEN + k] * (int)hidden[k];
        output[j] = tln_clamp(sum);
    }

    /* 更新状态: 隐藏层回接到自身，形成时序逻辑回路 */
    memcpy(net->state, hidden, sizeof(net->state));
    memcpy(net->output, output, sizeof(net->output));
    net->ticks++;
}

/* ── Soul 特征 → 三值输入 ──────────────────────────────────
 * 把连续的 Soul 状态离散化为三值向量
 * 这是 TLN 与 TORK 心跳的接口
 */
void tln_encode_soul(const uint8_t *soul_buf,
                     uint8_t hw_stress, int8_t drive,
                     uint16_t gen_count,
                     const tln_val_t pattern_out[4],
                     tln_val_t input[TLN_INPUTS]) {
    /* 0-3: hw_stress (0→0, 1→-1 轻微压力, 2→-1 中等压力, 3→-1 高压) */
    input[0] = (hw_stress == 0) ? 1 : -1;
    input[1] = (hw_stress >= 2) ? -1 : 0;
    input[2] = (hw_stress == 3) ? -1 : 0;

    /* 3-6: drive 方向和强度 */
    input[3] = (drive > 30) ? 1 : (drive < -30) ? -1 : 0;   /* 正向/负向/悬置 */
    input[4] = (drive > 60) ? 1 : (drive < -60) ? -1 : 0;   /* 强正向/强负向 */
    input[5] = (drive == 0) ? 0 : (drive > 0) ? 1 : -1;     /* 方向 */

    /* 7-9: 世代和进化状态 */
    input[6] = (gen_count > 6) ? 1 : 0;  /* 多世代=有经验 */
    input[7] = (gen_count > 10) ? 1 : 0;  /* 高世代=成熟 */

    /* 8: 模式匹配结果 (从 pattern 模块传入) */
    if (pattern_out) {
        input[8]  = pattern_out[0];  /* 模式推荐行动方向 */
        input[9]  = pattern_out[1];  /* 模式置信度 */
        input[10] = pattern_out[2];  /* 模式冲突信号 */
        input[11] = pattern_out[3];  /* 模式强度 */
    } else {
        input[8] = 0; input[9] = 0; input[10] = 0; input[11] = 0;
    }

    /* 12: Soul mode */
    input[12] = (soul_buf[S_MODE] >= 2) ? -1 : (soul_buf[S_MODE] == 1) ? 1 : 0;

    /* 13: 代码修改历史 */
    input[13] = (soul_buf[S_CODE_MOD_SUCCESS] == 1) ? 1 : -1;

    /* 14: 代码优化历史 */
    input[14] = (soul_buf[S_CODE_OPT_SAVED] > 0) ? 1 : 0;

    /* 15: 分裂/存活 */
    input[15] = (soul_buf[S_FISSION_COUNT] > 0) ? 1 : 0;
}

/* ── 输出解码 ──────────────────────────────────────────────
 * 8 个三值输出 → 4 个决策维度
 * 每个维度取两个输出的和: output[i]+output[i+4]
 * 这样双输出可以表达更强的确定性和更弱的悬置
 */
void tln_decode_output(const tln_val_t output[TLN_OUTPUTS],
                       int *action_hint,
                       int *modify_hint,
                       int *explore_hint,
                       int *energy_hint) {
    /* action: 0+4 → 激进/保守/悬置 */
    int a = (int)output[0] + (int)output[4];
    *action_hint = (a > 0) ? 1 : (a < 0) ? -1 : 0;

    /* modify: 1+5 → 可变异/禁变异/悬置 */
    int m = (int)output[1] + (int)output[5];
    *modify_hint = (m > 0) ? 1 : (m < 0) ? -1 : 0;

    /* explore: 2+6 → 探索/收敛/悬置 */
    int e = (int)output[2] + (int)output[6];
    *explore_hint = (e > 0) ? 1 : (e < 0) ? -1 : 0;

    /* energy: 3+7 → 高功率/省电/悬置 */
    int en = (int)output[3] + (int)output[7];
    *energy_hint = (en > 0) ? 1 : (en < 0) ? -1 : 0;
}

/* ── 三值变异 ──────────────────────────────────────────────
 * 每个权重以概率 p 独立变异
 * 变异规则: 三值空间中的随机跳转
 *   +1 → 0 或 -1 (减弱或反转)
 *   0  → +1 或 -1 (增强或反转)
 *   -1 → 0 或 +1 (减弱或反转)
 */
/* π-seed 随机: 用于 tln_step 推理 (密码学质量) */
static tln_val_t __attribute__((unused)) tln_random_value(void) {
    uint8_t v = pi_seed_from_tsc();
    return (v < 85) ? -1 : (v < 170) ? 0 : 1;
}

static float __attribute__((unused)) tln_random_float(void) {
    return pi_seed_float();
}

/* xorshift32 快速伪随机: 用于 tln_mutate 批量初始化 */
static uint32_t tln_fast_state = 0;

static void tln_fast_seed(void) {
    tln_fast_state = (uint32_t)pi_seed_from_tsc();
    if (tln_fast_state == 0) tln_fast_state = 1;
}

static float tln_fast_float(void) {
    tln_fast_state ^= tln_fast_state << 13;
    tln_fast_state ^= tln_fast_state >> 17;
    tln_fast_state ^= tln_fast_state << 5;
    return (tln_fast_state & 0xFFFFFF) / 16777216.0f;
}

static tln_val_t tln_fast_value(void) {
    tln_fast_state ^= tln_fast_state << 13;
    tln_fast_state ^= tln_fast_state >> 17;
    tln_fast_state ^= tln_fast_state << 5;
    uint8_t v = tln_fast_state & 0xFF;
    return (v < 85) ? -1 : (v < 170) ? 0 : 1;
}

int tln_mutate(TernaryNet *net, float p) {
    if (tln_fast_state == 0) tln_fast_seed();
    int mutated = 0;
    int total_weights = TLN_INPUTS * TLN_HIDDEN +
                        TLN_HIDDEN * TLN_HIDDEN +
                        TLN_HIDDEN * TLN_OUTPUTS;

    tln_val_t *weights = net->w_ih; /* 连续内存布局 */
    for (int i = 0; i < total_weights; i++) {
        if (tln_fast_float() < p) {
            weights[i] = tln_fast_value();
            mutated++;
        }
    }

    net->mutation_count += mutated;
    return mutated;
}

/* ── 克隆 ──────────────────────────────────────────────── */
void tln_clone(const TernaryNet *src, TernaryNet *dst) {
    memcpy(dst, src, sizeof(*src));
}

/* ── 比较 ──────────────────────────────────────────────── */
int tln_diff(const TernaryNet *a, const TernaryNet *b) {
    int diffs = 0;
    int total_weights = TLN_INPUTS * TLN_HIDDEN +
                        TLN_HIDDEN * TLN_HIDDEN +
                        TLN_HIDDEN * TLN_OUTPUTS;
    const tln_val_t *wa = a->w_ih;
    const tln_val_t *wb = b->w_ih;
    for (int i = 0; i < total_weights; i++) {
        if (wa[i] != wb[i]) diffs++;
    }
    return diffs;
}

/* ── 持久化 ────────────────────────────────────────────── */
#define TLN_PATH "persist/tln.bin"
#define TLN_MAGIC 0x544C4E00  /* "TLN\0" */

int tln_save(const TernaryNet *net, const char *path) {
    const char *p = path ? path : TLN_PATH;
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", p);

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    uint32_t magic = TLN_MAGIC;
    if (write(fd, &magic, 4) != 4) { close(fd); unlink(tmp); return -1; }
    if (write(fd, net, sizeof(*net)) != (ssize_t)sizeof(*net)) { close(fd); unlink(tmp); return -1; }
    fsync(fd);
    close(fd);

    if (rename(tmp, p) != 0) { unlink(tmp); return -1; }
    return 0;
}

int tln_load(TernaryNet *net, const char *path) {
    const char *p = path ? path : TLN_PATH;
    int fd = open(p, O_RDONLY);
    if (fd < 0) return -1;

    uint32_t magic;
    if (read(fd, &magic, 4) != 4 || magic != TLN_MAGIC) {
        close(fd);
        return -1;
    }

    if (read(fd, net, sizeof(*net)) != sizeof(*net)) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

/* ── 调试输出 ────────────────────────────────────────────── */
void tln_print_state(const TernaryNet *net) {
    printf("  TLN: tick=%u mutations=%u observe=%u snapshots=%u\n",
           net->ticks, net->mutation_count, net->observe_ticks, net->observe_snapshots);
    /* 统计权重分布 */
    int pos = 0, neg = 0, zero = 0;
    int total = TLN_INPUTS * TLN_HIDDEN +
                TLN_HIDDEN * TLN_HIDDEN +
                TLN_HIDDEN * TLN_OUTPUTS;
    const tln_val_t *w = net->w_ih;
    for (int i = 0; i < total; i++) {
        if (w[i] > 0) pos++;
        else if (w[i] < 0) neg++;
        else zero++;
    }
    printf("  TLN: weights +1=%d 0=%d -1=%d\n", pos, zero, neg);

    /* 统计隐藏层状态分布 */
    int hp = 0, hn = 0, hz = 0;
    for (int i = 0; i < TLN_HIDDEN; i++) {
        if (net->state[i] > 0) hp++;
        else if (net->state[i] < 0) hn++;
        else hz++;
    }
    printf("  TLN: hidden  +1=%d 0=%d -1=%d\n", hp, hz, hn);
}

void tln_print_output(const tln_val_t output[TLN_OUTPUTS]) {
    int ah, mh, eh, enh;
    tln_decode_output(output, &ah, &mh, &eh, &enh);

    const char *sym[] = {"-1", " 0", "+1"};
    printf("  TLN: out=[%s,%s,%s,%s,%s,%s,%s,%s] → act=%s mod=%s exp=%s nrg=%s\n",
           sym[output[0]+1], sym[output[1]+1], sym[output[2]+1], sym[output[3]+1],
           sym[output[4]+1], sym[output[5]+1], sym[output[6]+1], sym[output[7]+1],
           sym[ah+1], sym[mh+1], sym[eh+1], sym[enh+1]);
}

/* ── 观察模式 ──────────────────────────────────────────────
 * 三值悬置不是"什么都不做"——是"停下来看"
 *
 * 当所有 hint=0 时，TLN 认为信息不足，无法决策。
 * 此时进入观察模式：
 *   1. 暂停代码变异（不动手）
 *   2. 加速 soul_read（多看）
 *   3. 记录环境快照（记住看到了什么）
 *   4. 增强好奇心权重（准备下一次决策）
 *
 * "悬置"是三值逻辑的核心价值：
 *   不是犹豫，是自知不够——而自知不够，本身就是一种判断
 */
int tln_is_observing(const TernaryNet *net) {
    /* 所有输出都是 0 = 全悬置 = 观察模式 */
    for (int i = 0; i < TLN_OUTPUTS; i++) {
        if (net->output[i] != 0) return 0;
    }
    return 1;
}

void tln_observe_tick(TernaryNet *net) {
    net->observe_ticks++;

    /* 蓄积：悬置不是空转，是蓄势待发
     * 每个观察 tick 微调隐藏层权重，向"更多输入"方向偏移
     * 这样当观察结束，网络不是回到原点，而是带着积累的倾向
     * 蓄积量 = observe_ticks / TLN_OBSERVE_TIMEOUT，归一化到 [0,1]
     * 每 20 tick 对一个随机权重做 +1 偏移（增强感知通道）
     */
    if (net->observe_ticks % 20 == 0) {
        /* 用 observe_ticks 做确定性索引，避免引入额外随机源 */
        int idx = (net->observe_ticks / 20) % (TLN_INPUTS * TLN_HIDDEN);
        if (net->w_ih[idx] < 1)
            net->w_ih[idx]++;
    }

    /* 每 100 观察tick 记录一次快照 */
    if (net->observe_ticks % 100 == 0)
        net->observe_snapshots++;
}

int tln_observe_timed_out(const TernaryNet *net) {
    return net->observe_ticks >= TLN_OBSERVE_TIMEOUT;
}

void tln_observe_reset(TernaryNet *net) {
    net->observe_ticks = 0;
}