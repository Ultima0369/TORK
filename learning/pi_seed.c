#include "pi_seed.h"
#include <math.h>
#include <string.h>
#include <time.h>

/* ── 内联 RDTSC ──────────────────────────────────────────── */

static inline uint64_t rdtsc_now(void) {
    unsigned int lo, hi;
    __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi) :: "rcx");
    return ((uint64_t)hi << 32) | lo;
}

/* ── BBP 公式 ──────────────────────────────────────────────
 * Bailey-Borwein-Plouffe formula
 * 直接计算 π 的第 n 位十六进制数字
 * 不需要算出前面的位——这就是"可靠的不确定性"：
 * 你不需要遍历整条路，只要知道你在哪里
 */

static double bbp_series(uint64_t n, double j) {
    /* 16^(n-j) / (8*n + j) mod 1 的精确计算 */
    double s = 0.0;
    double pow16 = 1.0;  /* 16^(n-k) mod 1 */

    for (int64_t k = n; k >= 0; k--) {
        s += pow16 / (8.0 * k + j);
        s -= (int64_t)s;  /* mod 1 */
        pow16 /= 16.0;
    }

    /* k = n+1 onward — 快速收敛 */
    double term;
    int64_t k = (int64_t)n + 1;
    while (1) {
        term = pow16 / (8.0 * k + j);
        if (term < 1e-17) break;
        s += term;
        s -= (int64_t)s;
        pow16 /= 16.0;
        k++;
    }

    return s;
}

uint8_t pi_bbp_digit(uint64_t n) {
    /* π = 4*S1 - 2*S4 - S5 - S6  在第 n 位的十六进制 */
    double s1 = bbp_series(n, 1.0);
    double s4 = bbp_series(n, 4.0);
    double s5 = bbp_series(n, 5.0);
    double s6 = bbp_series(n, 6.0);

    double pi_n = 4.0 * s1 - 2.0 * s4 - s5 - s6;
    pi_n -= (int64_t)pi_n;  /* mod 1 */
    if (pi_n < 0) pi_n += 1.0;

    return (uint8_t)(pi_n * 16.0) & 0xF;
}

/* ── TSC 基线 ────────────────────────────────────────────── */

static uint64_t tsc_base = 0;

void pi_seed_init(void) {
    tsc_base = rdtsc_now();
}

/* ── 从 π 取值 ─────────────────────────────────────────────
 * 晶振拍了多少次 → 对应 π 的第几位
 * TSC 是物理时间在硅上的刻痕
 * π 是数学在最深处的指纹
 * 两者的交叉点，就是 TORK 的不确定性来源
 */

uint8_t pi_seed_from_tsc(void) {
    uint64_t tsc = rdtsc_now();
    uint64_t offset = tsc - tsc_base;

    /* 取 TSC 的高位变化部分（高位变化慢，低位变化快但太规律）
     * 用位混合让分布更均匀 */
    uint64_t mixed = offset;
    mixed ^= mixed >> 33;
    mixed *= 0xff51afd7ed558ccdULL;
    mixed ^= mixed >> 33;
    mixed *= 0xc4ceb9fe1a85ec53ULL;
    mixed ^= mixed >> 33;

    /* 用混合后的值作为 π 的索引
     * 取低 20 位 (0 ~ ~100万)，覆盖足够的 π 位数 */
    uint64_t pi_index = mixed & 0xFFFFF;

    /* 从 π 取两个相邻的十六进制位，拼成一个字节 */
    uint8_t hi = pi_bbp_digit(pi_index);
    uint8_t lo = pi_bbp_digit(pi_index + 1);
    return (hi << 4) | lo;
}

float pi_seed_float(void) {
    uint8_t byte = pi_seed_from_tsc();
    /* 映射到 (0, 1)，排除两端——0 和 1 是伪确定 */
    return (byte + 0.5f) / 256.0f;
}

/* ── 差异检测 ──────────────────────────────────────────────
 * "差异带来辨别，而有活动，才有生命"
 *
 * a == b → drift = 0  → 无差别，死寂
 * a != b → drift > 0  → 有辨别，活着
 * 差异越大，辨别越清晰
 */

float pi_drift(uint8_t a, uint8_t b) {
    int diff = (int)a - (int)b;
    if (diff < 0) diff = -diff;
    return (float)diff / 255.0f;
}

/* ── 反静态化衰减 ──────────────────────────────────────────
 * "地图有用，却绝不是领土"
 *
 * 模式置信度随时间衰减。
 * 不衰减 = 伪确定 = 把地图当领土
 * 衰减让 TORK 永远保持对世界的疑问
 *
 * half_life 是置信度减半所需的 tick 数
 */

float pi_decay(float confidence, uint32_t ticks_elapsed, uint32_t half_life) {
    if (half_life == 0) return 0.0f;
    float lambda = 0.69314718056f / (float)half_life;  /* ln(2) / half_life */
    return confidence * expf(-lambda * (float)ticks_elapsed);
}

/* ── 环境节律匹配 ──────────────────────────────────────────
 * "对万事万物的节律有正切认知和匹配"
 *
 * 万物自有节律。TORK 的节律是 drive 的变化，
 * 环境的节律是 π-seed 值的波动（物理时间在数学上的投影）。
 * 两者之间如果完全脱节，说明 TORK 在自说自话；
 * 两者如果共振，说明 TORK 在随世界呼吸。
 *
 * 老虎吼你，你就学到冒犯——这就是共振。
 * 万物塑造 TORK，TORK 回弹万物——这就是正切认知。
 */

void pi_rhythm_init(rhythm_tracker_t *rt) {
    memset(rt, 0, sizeof(rhythm_tracker_t));
}

void pi_rhythm_observe(rhythm_tracker_t *rt, uint8_t pi_val, int8_t drive_delta) {
    rt->values[rt->idx] = pi_val;
    rt->drive_deltas[rt->idx] = (uint8_t)(drive_delta >= 0 ? drive_delta : -drive_delta);
    rt->idx = (rt->idx + 1) % RHYTHM_WINDOW;
    if (rt->count < RHYTHM_WINDOW) rt->count++;
}

float pi_rhythm_dissonance(const rhythm_tracker_t *rt) {
    if (rt->count < 4) return 0.5f;  /* 数据不足，不做判断 */

    /* 计算环境节律：π-seed 值的平均变化量 */
    float env_rhythm = 0.0f;
    int env_changes = 0;
    for (int i = 1; i < rt->count; i++) {
        int prev = (rt->idx - rt->count + i - 1 + RHYTHM_WINDOW) % RHYTHM_WINDOW;
        int curr = (rt->idx - rt->count + i + RHYTHM_WINDOW) % RHYTHM_WINDOW;
        int diff = (int)rt->values[curr] - (int)rt->values[prev];
        if (diff < 0) diff = -diff;
        env_rhythm += (float)diff;
        env_changes++;
    }
    if (env_changes > 0) env_rhythm /= (float)env_changes;

    /* 计算 TORK 节律：drive 变化的平均幅度 */
    float tork_rhythm = 0.0f;
    for (int i = 0; i < rt->count; i++) {
        tork_rhythm += (float)rt->drive_deltas[i];
    }
    if (rt->count > 0) tork_rhythm /= (float)rt->count;

    /* 失调度：两者差异的归一化
     * 环境在大幅波动而 TORK 不动 = 严重脱节
     * TORK 在大幅波动而环境平静 = 躁动，也算脱节
     * 正切 = 两者幅度匹配
     */
    if (env_rhythm < 1.0f && tork_rhythm < 1.0f) return 0.0f;  /* 都平静，不算脱节 */

    float ratio;
    if (env_rhythm > tork_rhythm)
        ratio = tork_rhythm / env_rhythm;  /* TORK 不够响应 */
    else
        ratio = env_rhythm / tork_rhythm;  /* TORK 过度响应 */

    /* ratio ∈ [0,1]，1=完美匹配，0=完全脱节 → 失调度 = 1-ratio */
    return 1.0f - ratio;
}

/* ── π 波形特征指纹 ────────────────────────────────────────
 * "以振动频率识别万事万物"
 *
 * 不是加密，是天生的。
 * 坏人见过一次，π 波形就记住了。
 * 不管表面怎么伪装，振动频率对不上——伪装可以骗语义层，骗不了频率层。
 *
 * 好人被记住后，坏人的振动永远过不了那个相似度阈值。
 * 因为 π 指纹不是你选择的，是你在 π 空间的投影——你改不了自己的心跳。
 */

pi_profile_t pi_hash_profile(const uint8_t *buf, int len) {
    pi_profile_t prof;
    memset(&prof, 0, sizeof(prof));

    if (len < 2) return prof;

    /* 维度1：频率分布直方图 → 哪些振动频率占主导 (4 bytes)
     * 把 [0,255] 分成 16 个桶，统计每个桶的频次
     * 取频次最高的 4 个桶的索引作为特征 */
    uint8_t hist[16];
    memset(hist, 0, sizeof(hist));
    for (int i = 0; i < len; i++)
        hist[buf[i] >> 4]++;

    /* 找出频次最高的 4 个桶 */
    uint8_t top4[4] = {0, 0, 0, 0};
    uint8_t top4_count[4] = {0, 0, 0, 0};
    for (int b = 0; b < 16; b++) {
        for (int t = 0; t < 4; t++) {
            if (hist[b] > top4_count[t]) {
                /* 后移 */
                for (int s = 3; s > t; s--) {
                    top4[s] = top4[s-1];
                    top4_count[s] = top4_count[s-1];
                }
                top4[t] = (uint8_t)b;
                top4_count[t] = hist[b];
                break;
            }
        }
    }
    prof.digest[0] = (top4[0] << 4) | top4[1];
    prof.digest[1] = (top4[2] << 4) | top4[3];

    /* 维度2：变化率分布 → 波形在剧烈波动还是平稳 (4 bytes)
     * 计算相邻元素的差值，统计差值分布 */
    int delta_sum = 0, delta_max = 0;
    float delta_sq_sum = 0.0f;
    int zero_crossings = 0;
    for (int i = 1; i < len; i++) {
        int d = (int)buf[i] - (int)buf[i-1];
        if (d < 0) d = -d;
        delta_sum += d;
        if (d > delta_max) delta_max = d;
        delta_sq_sum += (float)d * (float)d;
        /* 过零检测：穿越中位线 128 */
        if ((buf[i-1] < 128 && buf[i] >= 128) ||
            (buf[i-1] >= 128 && buf[i] < 128))
            zero_crossings++;
    }
    int n_deltas = len - 1;
    float delta_mean = (n_deltas > 0) ? (float)delta_sum / n_deltas : 0.0f;
    float delta_var  = (n_deltas > 0) ? delta_sq_sum / n_deltas - delta_mean * delta_mean : 0.0f;

    prof.digest[2] = (uint8_t)(delta_mean * 2.0f);     /* 平均变化率 ×2 映射到 byte */
    prof.digest[3] = (uint8_t)(delta_max);               /* 最大跳变 */
    prof.digest[4] = (uint8_t)(sqrtf(delta_var) * 4.0f); /* 标准差 ×4 映射到 byte */
    prof.digest[5] = (uint8_t)(zero_crossings * 16 / (n_deltas > 0 ? n_deltas : 1)); /* 归一化过零率 */

    /* 维度3：极值间距 → 振动的节律周期 (4 bytes)
     * 找局部极大值，统计相邻极大值之间的间距
     * 间距分布 = 节律的指纹 */
    int peak_intervals[8];
    int peak_count = 0;
    int last_peak = -1;
    for (int i = 1; i < len - 1 && peak_count < 9; i++) {
        if (buf[i] > buf[i-1] && buf[i] > buf[i+1]) {
            if (last_peak >= 0 && peak_count < 8) {
                peak_intervals[peak_count] = i - last_peak;
                peak_count++;
            }
            last_peak = i;
        }
    }

    /* 将间距分布压缩到 2 bytes */
    if (peak_count > 0) {
        int pi_sum = 0, pi_min = len, pi_max = 0;
        for (int p = 0; p < peak_count; p++) {
            pi_sum += peak_intervals[p];
            if (peak_intervals[p] < pi_min) pi_min = peak_intervals[p];
            if (peak_intervals[p] > pi_max) pi_max = peak_intervals[p];
        }
        prof.digest[6] = (uint8_t)(pi_sum / peak_count);  /* 平均间距 */
        prof.digest[7] = (uint8_t)(pi_max - pi_min);       /* 间距散度 */
    } else {
        /* 没有峰值 = 平坦波形 */
        prof.digest[6] = 0;
        prof.digest[7] = 0;
    }

    /* 维度4：π 交叉权重 → 波形与 π 序列的相关性 (4 bytes)
     * 用 buf 的哈希值作为 π 索引，取 π 值与 buf 均值比较
     * 这一步让指纹与 π 本身纠缠——不是任意哈希，是 π 空间的投影 */
    uint32_t buf_hash = 0;
    for (int i = 0; i < len; i++) {
        buf_hash = buf_hash * 31 + buf[i];
    }
    /* 从 π 取 2 个值作为锚点 */
    uint8_t pi_anchor1 = pi_bbp_digit((uint64_t)(buf_hash & 0xFFFFF));
    uint8_t pi_anchor2 = pi_bbp_digit((uint64_t)((buf_hash >> 4) & 0xFFFFF));

    /* 计算 buf 与 π 锚点的统计交叉 */
    int above_pi1 = 0, above_pi2 = 0;
    float mean_val = 0.0f;
    for (int i = 0; i < len; i++) mean_val += (float)buf[i];
    mean_val /= (float)len;

    uint8_t pi1_threshold = pi_anchor1 << 4;
    uint8_t pi2_threshold = pi_anchor2 << 4;
    for (int i = 0; i < len; i++) {
        if (buf[i] > pi1_threshold) above_pi1++;
        if (buf[i] > pi2_threshold) above_pi2++;
    }

    prof.digest[8]  = pi_anchor1;
    prof.digest[9]  = pi_anchor2;
    prof.digest[10] = (uint8_t)(above_pi1 * 255 / (len > 0 ? len : 1)); /* π交叉率1 */
    prof.digest[11] = (uint8_t)(above_pi2 * 255 / (len > 0 ? len : 1)); /* π交叉率2 */

    /* 剩余 4 bytes：二级统计特征 */
    /* 12: 整体能量（值的平方和的归一化） */
    float energy = 0.0f;
    for (int i = 0; i < len; i++) energy += (float)buf[i] * (float)buf[i];
    prof.digest[12] = (uint8_t)(sqrtf(energy / (float)len) / sqrtf(255.0f * 255.0f) * 255.0f);

    /* 13: 偏度近似（均值与中位数的偏离方向） */
    uint8_t sorted_buf[256];
    int slen = (len > 256) ? 256 : len;
    memcpy(sorted_buf, buf, slen);
    /* 简单排序取中位数 */
    for (int a = 0; a < slen - 1; a++)
        for (int b = a + 1; b < slen; b++)
            if (sorted_buf[a] > sorted_buf[b]) {
                uint8_t tmp = sorted_buf[a];
                sorted_buf[a] = sorted_buf[b];
                sorted_buf[b] = tmp;
            }
    uint8_t median = sorted_buf[slen / 2];
    prof.digest[13] = (uint8_t)((mean_val - (float)median) + 128);  /* 偏移量编码 */

    /* 14-15: 混合校验 — 确保指纹的抗碰撞性 */
    uint16_t checksum = 0;
    for (int i = 0; i < 14; i++)
        checksum = checksum * 37 + prof.digest[i];
    prof.digest[14] = (uint8_t)(checksum >> 8);
    prof.digest[15] = (uint8_t)(checksum & 0xFF);

    return prof;
}

/* ── 指纹相似度 ────────────────────────────────────────────
 * 两个振动在 π 空间的距离
 * 1 = 同一个振动（同一类存在）
 * 0 = 完全不同的振动
 *
 * 坏人想伪装？振动频率对不上，就是过不了。
 * 伪装可以骗语义层，但骗不了频率层。
 */
float pi_profile_similarity(const pi_profile_t *a, const pi_profile_t *b) {
    if (!a || !b) return 0.0f;

    /* 逐字节 Hamming 相似度 */
    int matching_bits = 0;
    int total_bits = PI_PROFILE_SIZE * 8;
    for (int i = 0; i < PI_PROFILE_SIZE; i++) {
        uint8_t xor_val = a->digest[i] ^ b->digest[i];
        /* 计算匹配的位数 */
        matching_bits += 8 - __builtin_popcount(xor_val);
    }

    float raw_sim = (float)matching_bits / (float)total_bits;
    (void)raw_sim;

    /* 前 12 字节是特征，权重更高；后 4 字节是校验，权重更低 */
    int feature_bits = 0;
    int feature_total = 12 * 8;
    int check_bits = 0;
    int check_total = 4 * 8;
    for (int i = 0; i < 12; i++) {
        uint8_t xor_val = a->digest[i] ^ b->digest[i];
        feature_bits += 8 - __builtin_popcount(xor_val);
    }
    for (int i = 12; i < PI_PROFILE_SIZE; i++) {
        uint8_t xor_val = a->digest[i] ^ b->digest[i];
        check_bits += 8 - __builtin_popcount(xor_val);
    }

    float feature_sim = (float)feature_bits / (float)feature_total;
    float check_sim   = (float)check_bits / (float)check_total;

    /* 特征 70%，校验 30% */
    return feature_sim * 0.7f + check_sim * 0.3f;
}

/* 从 rhythm_tracker 直接生成指纹 */
pi_profile_t pi_profile_from_rhythm(const rhythm_tracker_t *rt) {
    if (!rt || rt->count == 0) {
        pi_profile_t empty;
        memset(&empty, 0, sizeof(empty));
        return empty;
    }
    return pi_hash_profile(rt->values, rt->count);
}

/* ── π-spiral：在无限中选重要 ──────────────────────────────
 * "既然无限是可以确认了，其中的有限，就合理"
 *
 * 不是连续取，是按斐波那契间距取。
 * 斐波那契是自然选的间距——1,1,2,3,5,8,13...
 * 每一步的跨度越来越大，但每一步都是前两步之和。
 * 这就是"在无限中选有限"：不遍历，只取结构性的点。
 */
pi_spiral_t pi_spiral(uint64_t start) {
    pi_spiral_t sp;
    memset(&sp, 0, sizeof(sp));

    /* 斐波那契数列 */
    uint64_t fib[PI_SPIRAL_MAX];
    fib[0] = 1; fib[1] = 1;
    for (int i = 2; i < PI_SPIRAL_MAX; i++) {
        fib[i] = fib[i-1] + fib[i-2];
        /* 防止溢出 */
        if (fib[i] > 0xFFFFFULL) {
            sp.count = i;
            break;
        }
    }
    if (sp.count == 0) sp.count = PI_SPIRAL_MAX;

    for (int i = 0; i < sp.count; i++) {
        sp.indices[i] = start + fib[i];
        sp.values[i] = pi_bbp_digit(sp.indices[i]);
    }

    return sp;
}

/* ── 骨架提取：π 的神圣几何 ────────────────────────────────
 * 正多边形是圆的有限截取，骨架是 π 的有限截取
 * 不是存所有位，只存结构——转折点、对称性、跳跃节律
 */
pi_skeleton_t pi_extract_skeleton(const pi_spiral_t *sp) {
    pi_skeleton_t sk;
    memset(&sk, 0, sizeof(sk));

    if (!sp || sp->count < 3) return sk;

    /* 转折点：方向翻转的次数 */
    int turns = 0;
    int rising_count = 0, falling_count = 0;
    for (int i = 1; i < sp->count; i++) {
        if (sp->values[i] > sp->values[i-1]) {
            rising_count++;
            if (i > 1 && sp->values[i-1] <= sp->values[i-2]) turns++;
        } else if (sp->values[i] < sp->values[i-1]) {
            falling_count++;
            if (i > 1 && sp->values[i-1] >= sp->values[i-2]) turns++;
        }
    }
    sk.turns = (uint8_t)(turns > 255 ? 255 : turns);

    /* 对称度：spiral 前半段和后半段（反转后）的匹配度 */
    int half = sp->count / 2;
    int sym_match = 0;
    for (int i = 0; i < half; i++) {
        int diff = (int)sp->values[i] - (int)sp->values[sp->count - 1 - i];
        if (diff < 0) diff = -diff;
        if (diff <= 4) sym_match++;  /* 容差 4/15 ≈ 27% */
    }
    sk.symmetry = (uint8_t)(sym_match * 255 / (half > 0 ? half : 1));

    /* 最大跳跃 */
    int max_leap = 0;
    float leap_sum = 0.0f;
    float leap_sq_sum = 0.0f;
    for (int i = 1; i < sp->count; i++) {
        int leap = (int)sp->values[i] - (int)sp->values[i-1];
        if (leap < 0) leap = -leap;
        if (leap > max_leap) max_leap = leap;
        leap_sum += (float)leap;
        leap_sq_sum += (float)leap * (float)leap;
    }
    sk.leap_max = (uint8_t)max_leap;

    /* 跳跃节律：跳跃间隔的方差 → 节奏的稳定性 */
    int n_leaps = sp->count - 1;
    float leap_mean = (n_leaps > 0) ? leap_sum / n_leaps : 0.0f;
    float leap_var = (n_leaps > 0) ? leap_sq_sum / n_leaps - leap_mean * leap_mean : 0.0f;
    /* 方差小 = 节奏稳定（有规律），方差大 = 节奏混乱 */
    sk.leap_rhythm = (uint8_t)(sqrtf(leap_var) * 16.0f);  /* 映射到 byte */

    /* 黄金比例：上升段/下降段的比值
     * 接近 φ ≈ 1.618 = "神圣几何" */
    sk.golden_ratio = (falling_count > 0) ?
                      (float)rising_count / (float)falling_count :
                      (float)rising_count;

    return sk;
}

/* 骨架相似度 */
float pi_skeleton_similarity(const pi_skeleton_t *a, const pi_skeleton_t *b) {
    if (!a || !b) return 0.0f;

    /* 转折点相似度 */
    int turn_diff = (int)a->turns - (int)b->turns;
    if (turn_diff < 0) turn_diff = -turn_diff;
    float turn_sim = 1.0f - (float)turn_diff / 32.0f;
    if (turn_sim < 0) turn_sim = 0;

    /* 对称度相似度 */
    int sym_diff = (int)a->symmetry - (int)b->symmetry;
    if (sym_diff < 0) sym_diff = -sym_diff;
    float sym_sim = 1.0f - (float)sym_diff / 255.0f;

    /* 跳跃模式相似度 */
    int leap_diff = (int)a->leap_max - (int)b->leap_max;
    if (leap_diff < 0) leap_diff = -leap_diff;
    float leap_sim = 1.0f - (float)leap_diff / 15.0f;
    if (leap_sim < 0) leap_sim = 0;

    /* 黄金比例相似度 */
    float gr_diff = a->golden_ratio - b->golden_ratio;
    if (gr_diff < 0) gr_diff = -gr_diff;
    float gr_sim = 1.0f - gr_diff / 3.0f;
    if (gr_sim < 0) gr_sim = 0;

    /* 加权：转折(25%) + 对称(25%) + 跳跃(20%) + 黄金比(30%) */
    return turn_sim * 0.25f + sym_sim * 0.25f + leap_sim * 0.20f + gr_sim * 0.30f;
}

/* ── 水晶裂变：结构加不确定 ────────────────────────────────
 * "真复杂与不得不简化，而有新奇特"
 *
 * 纯 2^n 是简化（呆板），纯随机是复杂（混乱）
 * 结构 + π 不确定 = 真复杂（有生命的裂变）
 */
crystal_cell_t crystal_fission(const crystal_cell_t *current, float base_rate) {
    crystal_cell_t next;
    memset(&next, 0, sizeof(next));

    if (!current) return next;

    next.generation = current->generation + 1;

    /* 基础分裂：base_rate^n 的种群增长 */
    float base_growth = powf(base_rate, (float)next.generation * 0.1f);

    /* π 微调：不是随机扰动，是结构性的不确定
     * 从 π 取当前代数对应的位，作为活力调制 */
    uint64_t pi_idx = (uint64_t)next.generation * 7ULL + 3ULL;  /* 7=素数间距 */
    uint8_t pi_digit = pi_bbp_digit(pi_idx);
    float pi_mod = (pi_digit + 0.5f) / 16.0f;  /* ∈ (0,1) */

    /* 活力 = 基础活力 × π 调制
     * π_digit 接近 8 → 活力接近 1.0（兴旺）
     * π_digit 接近 0 → 活力接近 0.5（抑制）
     * 这不是随机——是 π 在决定哪一代兴旺、哪一代抑制 */
    next.vitality = 0.5f + 0.5f * pi_mod;

    /* 种群 = 基础增长 × 活力 */
    next.population = (uint32_t)((float)current->population * base_growth * next.vitality);

    /* 种群上限：防止无限增长（有限截取） */
    if (next.population > 1000000) next.population = 1000000;

    return next;
}

/* ── 3.5R 震荡检测 ─────────────────────────────────────────
 * "和而不同"不是道德宣言，是数学事实
 *
 * 计算 TORK 的有效 R 值：
 * 把 drive 变化序列映射到 logistic 参数空间
 * 方法：统计 drive 的 unique 值数量和变化率
 *
 * R < 3   → drive 重复同一值（unique≈1）→ 僵死
 * R ≈ 3.5 → drive 在 2-4 个值间震荡    → 活着
 * R > 3.57 → drive 有更多 unique 值     → 混沌中的秩序
 * R ≈ 3.82 → 混沌中突然出现周期模式     → 周期窗口
 */
float pi_logistic_R(const rhythm_tracker_t *rt) {
    if (!rt || rt->count < 4) return 2.8f;  /* 数据不足，默认 R<3（保守） */

    /* 统计 drive 变化的 unique 值数量 */
    uint8_t seen[256];
    memset(seen, 0, sizeof(seen));
    int unique = 0;
    for (int i = 0; i < rt->count; i++) {
        if (!seen[rt->drive_deltas[i]]) {
            seen[rt->drive_deltas[i]] = 1;
            unique++;
        }
    }

    /* 统计 drive 变化的平均幅度 */
    float delta_mean = 0.0f;
    for (int i = 0; i < rt->count; i++)
        delta_mean += (float)rt->drive_deltas[i];
    delta_mean /= (float)rt->count;

    /* 映射到 R 值：
     * unique 少 + delta 小 → R 低（僵死）
     * unique 多 + delta 中 → R 中（活着）
     * unique 多 + delta 大 → R 高（混沌）
     *
     * 经验映射公式：
     *   R = 2.5 + 0.3 * unique + 0.05 * delta_mean
     *   unique=1, delta=0  → R=2.8  (僵死)
     *   unique=4, delta=5  → R=3.95 (活着)
     *   unique=8, delta=15 → R=5.45 (混沌)
     */
    float R = 2.5f + 0.3f * (float)unique + 0.05f * delta_mean;

    /* 检测周期窗口：如果有连续 3 个以上相同的 drive 变化模式 */
    if (rt->count >= 6) {
        int period_2 = 0;
        int period_3 = 0;
        for (int i = 2; i < rt->count; i++) {
            if (rt->drive_deltas[i] == rt->drive_deltas[i-2]) period_2++;
        }
        for (int i = 3; i < rt->count; i++) {
            if (rt->drive_deltas[i] == rt->drive_deltas[i-3]) period_3++;
        }
        /* 周期窗口：在混沌（R>3.57）中检测到周期性 */
        if (R > 3.57f && period_2 > rt->count / 3) {
            R = 3.82f;  /* 标记为周期窗口 */
        }
    }

    /* R 值上限：不是无限混沌，是有限截取 */
    if (R > 4.0f) R = 4.0f;

    return R;
}

r_zone_t pi_r_zone(const rhythm_tracker_t *rt) {
    float R = pi_logistic_R(rt);

    if (R < 3.0f)  return R_DEAD;
    if (R < 3.45f) return R_ALIVE;   /* R≈3.5 震荡区间 */
    if (R < 3.75f) return R_CHAOS;   /* R>3.57 混沌 */
    return R_WINDOW;                  /* R≈3.82 周期窗口 */
}

const char *pi_r_zone_advice(r_zone_t zone) {
    switch (zone) {
    case R_DEAD:   return "僵死 — 强制注入好奇，打破稳态";
    case R_ALIVE:  return "活着 — 保持震荡，和而不同";
    case R_CHAOS:  return "混沌 — 降低频率，在无序中寻找模式";
    case R_WINDOW: return "窗口 — 积极探索，周期窗口是宝贵秩序";
    default:       return "未知";
    }
}
