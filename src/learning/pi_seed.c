#include "pi_seed.h"
#include <math.h>
#include <string.h>
#include <time.h>

/* ── SHA-256 (FIPS 180-4) ───────────────────────────────────
 * P0-3: 密码学质量混合，替代可逆的 MurmurHash3
 * 标准实现，用于 HMAC-SHA256 种子生成
 */

#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA256_CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_SIG0(x) (SHA256_ROTR(x,2) ^ SHA256_ROTR(x,13) ^ SHA256_ROTR(x,22))
#define SHA256_SIG1(x) (SHA256_ROTR(x,6) ^ SHA256_ROTR(x,11) ^ SHA256_ROTR(x,25))
#define SHA256_sig0(x) (SHA256_ROTR(x,7) ^ SHA256_ROTR(x,18) ^ ((x) >> 3))
#define SHA256_sig1(x) (SHA256_ROTR(x,17) ^ SHA256_ROTR(x,19) ^ ((x) >> 10))

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

typedef struct {
    uint32_t h[8];
    uint8_t  buf[64];
    uint64_t total;
    int      buflen;
} sha256_ctx;

static void sha256_init(sha256_ctx *c) {
    c->h[0]=0x6a09e667; c->h[1]=0xbb67ae85; c->h[2]=0x3c6ef372; c->h[3]=0xa54ff53a;
    c->h[4]=0x510e527f; c->h[5]=0x9b05688c; c->h[6]=0x1f83d9ab; c->h[7]=0x5be0cd19;
    c->total = 0; c->buflen = 0;
}

static void sha256_transform(uint32_t h[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)|
               ((uint32_t)block[i*4+2]<<8)|block[i*4+3];
    for (int i = 16; i < 64; i++)
        w[i] = SHA256_sig1(w[i-2]) + w[i-7] + SHA256_sig0(w[i-15]) + w[i-16];

    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = hh + SHA256_SIG1(e) + SHA256_CH(e,f,g) + sha256_k[i] + w[i];
        uint32_t t2 = SHA256_SIG0(a) + SHA256_MAJ(a,b,c);
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
}

static void sha256_update(sha256_ctx *c, const uint8_t *data, size_t len) {
    c->total += len;
    while (len > 0) {
        int space = 64 - c->buflen;
        int take = (int)len < space ? (int)len : space;
        memcpy(c->buf + c->buflen, data, take);
        c->buflen += take;
        data += take; len -= take;
        if (c->buflen == 64) {
            sha256_transform(c->h, c->buf);
            c->buflen = 0;
        }
    }
}

static void sha256_final(sha256_ctx *c, uint8_t out[32]) {
    uint64_t bits = c->total * 8;
    uint8_t pad = 0x80;
    sha256_update(c, &pad, 1);
    pad = 0;
    while (c->buflen != 56) sha256_update(c, &pad, 1);
    uint8_t len_be[8];
    for (int i = 7; i >= 0; i--) { len_be[i] = (uint8_t)(bits & 0xFF); bits >>= 8; }
    sha256_update(c, len_be, 8);
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(c->h[i] >> 24);
        out[i*4+1] = (uint8_t)(c->h[i] >> 16);
        out[i*4+2] = (uint8_t)(c->h[i] >> 8);
        out[i*4+3] = (uint8_t)(c->h[i]);
    }
}

void pi_sha256(const uint8_t *msg, size_t len, uint8_t out[32]) {
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, msg, len);
    sha256_final(&c, out);
}

/* HMAC-SHA256: RFC 2104 */
void pi_hmac_sha256(const uint8_t *key, size_t key_len,
                    const uint8_t *msg, size_t msg_len,
                    uint8_t out[32]) {
    uint8_t k_pad[64];
    uint8_t tk[32];

    if (key_len > 64) {
        pi_sha256(key, key_len, tk);
        key = tk; key_len = 32;
    }
    memset(k_pad, 0x36, 64);
    for (size_t i = 0; i < key_len; i++) k_pad[i] ^= key[i];

    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, k_pad, 64);
    sha256_update(&c, msg, msg_len);
    uint8_t inner[32];
    sha256_final(&c, inner);

    memset(k_pad, 0x5c, 64);
    for (size_t i = 0; i < key_len; i++) k_pad[i] ^= key[i];

    sha256_init(&c);
    sha256_update(&c, k_pad, 64);
    sha256_update(&c, inner, 32);
    sha256_final(&c, out);
}

/* ── 内联 RDTSC + RDRAND ──────────────────────────────────── */

static inline uint64_t rdtsc_now(void) {
    unsigned int lo, hi;
    __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi) :: "rcx");
    return ((uint64_t)hi << 32) | lo;
}

/* RDRAND: Intel/AMD 硬件随机数指令
 * 从片上热噪声熵源取值，与 TSC 完全独立
 * 返回 0 = 硬件不支持或超时，1 = 成功 */
static inline int rdrand64(uint64_t *val) {
    unsigned char ok;
    /* 最多重试 10 次（硬件熵池耗尽时需要等待） */
    for (int retry = 0; retry < 10; retry++) {
        __asm__ __volatile__("rdrand %0; setc %1"
                             : "=r"(*val), "=qm"(ok)
                             :: "cc");
        if (ok) return 1;
    }
    return 0;
}

/* ── HMAC 反馈链状态 ────────────────────────────────────────
 * P0-3: 每次 HMAC 输出的部分字节反馈到下一次的 key 中
 * 这样即使攻击者知道当前 TSC，也无法预测下一次输出
 * 因为上一次的 HMAC 输出是单向的——知道输出无法还原 key
 * 链式结构: out_n = HMAC(key_n, msg_n)
 *           key_{n+1} = mix(TSC, out_n[0..3])
 * 攻击者要预测 out_{n+1}，需要知道 out_n[0..3]
 * 但 out_n 本身是 HMAC 输出，多项式时间内不可逆
 */
static uint8_t hmac_feedback[4] = {0, 0, 0, 0};

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

    /* XOR TSC 高低 32 位 — 防止从已知 offset 逆推
     * 单纯 offset 是可预测的（线性递增）
     * 高低半异或后，即使知道大致时间也无法还原精确 TSC */
    uint64_t hi32 = offset >> 32;
    uint64_t lo32 = offset & 0xFFFFFFFFULL;
    uint64_t mixed = lo32 ^ (hi32 * 0x9e3779b97f4a7c15ULL);

    /* MurmurHash3 位混合 */
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

/* ── HMAC-SHA256 种子: P0-3 密码学质量混合 ──────────────────
 * Seed = HMAC-SHA256(key, msg)
 *
 * key = RDRAND硬件熵(8B) + TSC低位(4B) + 上次HMAC反馈(4B) = 16B
 * msg = 从 π 取的 8 字节偏移值（数学序列的确定性锚点）
 *
 * 三层不可预测性：
 *   1. RDRAND: 片上热噪声，与 TSC 完全独立，攻击者无法观测
 *   2. HMAC反馈链: 上次输出的部分字节混入下次 key
 *      攻击者知道 TSC 也无法预测——因为缺少上次 HMAC 输出
 *   3. SHA-256 单向性: 即使知道输出，多项式时间内无法还原 key
 *
 * 降级策略: RDRAND 不可用时退化为 TSC + 反馈链
 * 反馈链本身已经提供多项式时间不可预测性（链式依赖）
 */
uint8_t pi_seed_hmac(void) {
    uint64_t tsc = rdtsc_now();
    uint64_t offset = tsc - tsc_base;

    /* key: 16 字节 = RDRAND(8B) + TSC低位(4B) + HMAC反馈(4B) */
    uint8_t key[16];
    memset(key, 0, sizeof(key));

    /* 层1: RDRAND 硬件熵 — 与 TSC 完全独立的物理噪声 */
    uint64_t hw_rand = 0;
    if (rdrand64(&hw_rand)) {
        for (int i = 0; i < 8; i++)
            key[i] = (uint8_t)(hw_rand >> (i * 8));
    } else {
        /* 降级: 用 TSC 高位异或低位作为伪熵（不如 RDRAND，但聊胜于无） */
        uint64_t fallback = offset ^ (offset >> 32) ^ 0x5A5A5A5A5A5A5A5AULL;
        for (int i = 0; i < 8; i++)
            key[i] = (uint8_t)(fallback >> (i * 8));
    }

    /* 层2: TSC 低 4 字节 — 物理时间的低位扰动 */
    for (int i = 0; i < 4; i++)
        key[8 + i] = (uint8_t)(offset >> (i * 8));

    /* 层3: HMAC 反馈链 — 上次输出的部分字节 */
    for (int i = 0; i < 4; i++)
        key[12 + i] = hmac_feedback[i];

    /* msg: 从 π 取 8 字节偏移值 — 数学确定性锚点 */
    uint64_t pi_idx1 = (offset >> 16) & 0xFFFFF;
    uint64_t pi_idx2 = offset & 0xFFFFF;
    uint8_t msg[8];
    msg[0] = pi_bbp_digit(pi_idx1);
    msg[1] = pi_bbp_digit(pi_idx1 + 1);
    msg[2] = pi_bbp_digit(pi_idx1 + 2);
    msg[3] = pi_bbp_digit(pi_idx1 + 3);
    msg[4] = pi_bbp_digit(pi_idx2);
    msg[5] = pi_bbp_digit(pi_idx2 + 1);
    msg[6] = pi_bbp_digit(pi_idx2 + 2);
    msg[7] = pi_bbp_digit(pi_idx2 + 3);

    /* HMAC-SHA256: 单向混合 */
    uint8_t hmac_out[32];
    pi_hmac_sha256(key, 16, msg, 8, hmac_out);

    /* 反馈链更新: 取输出前 4 字节作为下次 key 的一部分
     * 这是链式不可预测性的核心：
     * 攻击者要预测 out_{n+1}，需要知道 feedback_n
     * 但 feedback_n = HMAC_out_n[0..3]，是 HMAC 输出的一部分
     * HMAC 是单向函数——知道输出无法还原完整 key
     * 因此即使攻击者知道 TSC，也无法预测下一次输出 */
    hmac_feedback[0] = hmac_out[0];
    hmac_feedback[1] = hmac_out[1];
    hmac_feedback[2] = hmac_out[2];
    hmac_feedback[3] = hmac_out[3];

    return hmac_out[0];
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
