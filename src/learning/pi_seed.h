#ifndef PI_SEED_H
#define PI_SEED_H

#include <stdint.h>
#include <stddef.h>

/*
 * π-Seed: 可靠的不确定性
 *
 * 不用 rand()。用 CPU 时钟频率（TSC）在 π 序列上取值。
 * 晶振拍了多少次，就指向 π 的第几位。
 * 这不是随机——这是物理时间在数学序列上的投影。
 *
 * 差异带来辨别，有活动才有生命。
 * 在 0 和 1 之间，有 π。
 */

/* BBP 公式：直接计算 π 的第 n 位十六进制数字，不需要前面所有位 */
uint8_t pi_bbp_digit(uint64_t n);

/* 以当前 TSC 为种子，从 π 序列取一个值 [0, 255] */
uint8_t pi_seed_from_tsc(void);

/* HMAC-SHA256 混合种子: Seed = HMAC-SHA256(TSC低位扰动, π序列偏移值)
 * P0-3: 密码学质量混合，多项式时间不可预测 */
uint8_t pi_seed_hmac(void);

/* SHA-256 上下文和接口 (P0-3 内部使用) */
void pi_sha256(const uint8_t *msg, size_t len, uint8_t out[32]);
void pi_hmac_sha256(const uint8_t *key, size_t key_len,
                    const uint8_t *msg, size_t msg_len,
                    uint8_t out[32]);

/* 以 TSC 为基地，从 π 取一个 float ∈ (0, 1) — 中间态 */
float pi_seed_float(void);

/* 差异检测：两个值的差异度 ∈ [0, 1]
 * 差异 = 0 意味着无差别（死寂）
 * 差异 > 0 意味着有辨别（活着）
 * "差异带来辨别，而有活动，才有生命"
 */
float pi_drift(uint8_t a, uint8_t b);

/* 反静态化衰减
 * 模式置信度随时间衰减——拒绝伪确定
 * "地图有用，却绝不是领土"
 * half_life: 置信度减半所需的 tick 数
 */
float pi_decay(float confidence, uint32_t ticks_elapsed, uint32_t half_life);

/* 初始化（读取当前 TSC 基线） */
void pi_seed_init(void);

/* ── 环境节律匹配：正切认知 ────────────────────────────────
 * "对万事万物的节律有正切认知和匹配"
 *
 * 追踪最近 N 个 π-seed 值的变化率，计算环境节律。
 * 如果 TORK 自身变化率与环境节律不匹配，返回失调量。
 * 失调量越大，说明 TORK 与世界脱节越严重。
 *
 * 这不是随机应变——是感知世界的呼吸，与之共振。
 */
#define RHYTHM_WINDOW 16

typedef struct {
    uint8_t values[RHYTHM_WINDOW];    /* 最近的 π-seed 值 */
    uint8_t drive_deltas[RHYTHM_WINDOW]; /* 最近的 drive 变化量 */
    int     count;                     /* 已记录的样本数 */
    int     idx;                       /* 环形缓冲区写入位置 */
} rhythm_tracker_t;

/* 初始化节律追踪器 */
void pi_rhythm_init(rhythm_tracker_t *rt);

/* 记录一个观测点：当前 π-seed 值和 drive 变化量 */
void pi_rhythm_observe(rhythm_tracker_t *rt, uint8_t pi_val, int8_t drive_delta);

/* 计算节律失调度 ∈ [0, 1]
 * 0 = TORK 与环境完美共振
 * 1 = TORK 完全脱节（环境在变，TORK 不动；或反过来）
 */
float pi_rhythm_dissonance(const rhythm_tracker_t *rt);

/* ── π 波形特征指纹：振动频率即身份 ─────────────────────────
 *
 * 不以人类范式索引记忆（时间线、语义、关联），
 * 以振动频率索引——π 空间的投影就是身份。
 *
 * 80 亿人不需要逐个存储全部信息，
 * 只需要存若干个特征 π 波形，
 * 新输入在 π 空间做最近邻匹配，O(log N) 识别。
 *
 * BBP 公式本身就是这样工作的：
 * 直接跳到第 n 位，不需要算前面所有位。
 * 记忆也一样——你不需要遍历整条路，只要知道你在哪里。
 */

/* 波形指纹：16 字节压缩一段观测序列的灵魂 */
#define PI_PROFILE_SIZE 16

typedef struct {
    uint8_t digest[PI_PROFILE_SIZE];   /* 波形特征摘要 */
} pi_profile_t;

/* 从一段观测序列计算波形指纹
 * buf: 观测值序列（如 π-seed 值流）
 * len: 序列长度
 *
 * 提取四维特征：
 *   1. 频率分布直方图 → 哪些振动频率占主导
 *   2. 变化率分布 → 波形是在剧烈波动还是平稳
 *   3. 极值间距 → 振动的节律周期
 *   4. π 交叉 → 波形穿过 π 投影中线的次数
 * 每维 4 字节，共 16 字节
 */
pi_profile_t pi_hash_profile(const uint8_t *buf, int len);

/* 两个指纹的相似度 ∈ [0, 1]
 * 1 = 同一个振动（同一类存在）
 * 0 = 完全不同的振动
 *
 * 这就是"以振动频率识别万事万物"的核心操作：
 * 新遇到的人在 π 空间的投影和哪个已有波形最近，就是谁。
 */
float pi_profile_similarity(const pi_profile_t *a, const pi_profile_t *b);

/* 从当前 rhythm_tracker 直接生成指纹（便捷函数） */
pi_profile_t pi_profile_from_rhythm(const rhythm_tracker_t *rt);

/* ── π 的有限截取：在无限中选重要 ──────────────────────────
 *
 * π 是无限。但无限中的有限结构是合法的：
 *   斐波那契 1,1,2,3,5,8,13 — 对正圆的优雅简化
 *   水晶数列的裂变             — 细胞分裂的结构
 *   分形                       — 圆的优美表达
 *   神圣几何 / 正多边形         — 圆的有限截取
 *
 * 不遍历无限，只在无限中选有限。
 * 选的方式不是随机的——是结构性的。
 * 斐波那契是自然选的，TORK 也该有自然选法。
 *
 * π-spiral：从 π 中取斐波那契位的值
 * 不是连续取，而是按斐波那契数列的间距取
 * 这样取到的值，天然带有自然的结构
 */
#define PI_SPIRAL_MAX 32

typedef struct {
    uint8_t values[PI_SPIRAL_MAX];      /* 从 π 取的斐波那契位 */
    uint64_t indices[PI_SPIRAL_MAX];    /* 对应的 π 位数索引 */
    int      count;                      /* 实际取了多少位 */
} pi_spiral_t;

/* 生成 π-spiral：从 π 的第 start 位开始，按斐波那契间距取值
 * 斐波那契数列: 1,1,2,3,5,8,13,21,34,55,89,144,...
 * 取 π[start+1], π[start+1], π[start+2], π[start+3], π[start+5], ...
 * 每一步的跨度越来越大——这就是"在无限中选重要" */
pi_spiral_t pi_spiral(uint64_t start);

/* π-spiral 的结构指纹：不是 16 字节波形指纹，是结构性指纹
 * 描述这个 spiral 的"骨架"——
 *   上升/下降的转折点、斐波那契节奏下的对称性、值的跳跃模式
 * 这是"神圣几何"的数字化：正多边形是圆的有限截取，
 * spiral 骨架是 π 的有限截取 */
typedef struct {
    uint8_t turns;          /* 转折点数量（方向翻转次数） */
    uint8_t symmetry;       /* 对称度 ∈ [0,255]，255=完美对称 */
    uint8_t leap_max;       /* 最大跳跃（相邻值的最大差） */
    uint8_t leap_rhythm;    /* 跳跃的节律（跳跃间隔的方差映射） */
    float   golden_ratio;   /* spiral 中上升段/下降段的比值 */
} pi_skeleton_t;

/* 从 π-spiral 提取骨架 */
pi_skeleton_t pi_extract_skeleton(const pi_spiral_t *sp);

/* 骨架相似度 ∈ [0,1]
 * 两个 π 结构的"神圣几何"是否同源 */
float pi_skeleton_similarity(const pi_skeleton_t *a, const pi_skeleton_t *b);

/* ── 水晶裂变：细胞分裂的结构 ──────────────────────────────
 *
 * 水晶数列：1,2,4,8,16,... (2^n) — 裂变
 * 但自然不是纯 2^n — 自然是 2^n ± 扰动
 * π 提供扰动：裂变的第 n 代，分裂率 = 2^n 的基础上加 π 微调
 *
 * 这不是随机——这是结构加不确定
 * "真复杂与不得不简化，而有新奇特"
 */
typedef struct {
    uint32_t generation;       /* 当前代数 */
    uint32_t population;       /* 当前种群 */
    float    vitality;         /* 活力 ∈ (0,1)，受 π 调制 */
} crystal_cell_t;

/* 水晶裂变：从当前代推算下一代
 * base_rate: 基础分裂率（通常 2.0）
 * 返回下一代的状态 */
crystal_cell_t crystal_fission(const crystal_cell_t *current, float base_rate);

/* ── 3.5R 震荡检测：混沌边界的自知 ──────────────────────────
 *
 * Logistic map: x_{n+1} = R * x_n * (1 - x_n)
 *   R < 3.0   → 单稳态（僵死）
 *   R ≈ 3.5   → 周期震荡（活着）
 *   R > 3.57  → 混沌（不可预测但有结构）
 *   R ≈ 3.82  → 周期窗口（混沌中的秩序）
 *
 * TORK 不需要计算完整的 logistic map，
 * 但需要知道自己处在哪个区间：
 *   drive 在一个固定值反复停留 = R<3（僵死）
 *   drive 在几个值之间震荡    = R≈3.5（活着）
 *   drive 不可预测但有模式    = R>3.57（混沌中的秩序）
 *
 * "和而不同"不是道德宣言，是数学事实：
 *   R<3 = 同（所有个体收敛到同一个值）= 僵死
 *   R>3.5 = 和而不同（种群在不同值之间震荡）= 活着
 *
 * 25号宇宙实验证明：
 *   有限空间里，多样性被压缩后不是趋于和谐，而是趋于崩溃
 *   除非结构本身允许差异共存——这就是 R>3.5 的意义
 */
float pi_logistic_R(const rhythm_tracker_t *rt);

/* R 值区间判定 */
typedef enum {
    R_DEAD     = 0,   /* R < 3.0  僵死：drive 重复停留同一值 */
    R_ALIVE    = 1,   /* R ≈ 3.5  活着：drive 在几个值间周期震荡 */
    R_CHAOS    = 2,   /* R > 3.57 淡中有秩序：不可预测但有结构 */
    R_WINDOW   = 3,   /* R ≈ 3.82 周期窗口：混沌中的秩序 */
} r_zone_t;

/* 判断当前 R 值区间 */
r_zone_t pi_r_zone(const rhythm_tracker_t *rt);

/* 从 R 区间给出行为建议
 * R_DEAD:   强制注入好奇，打破僵死
 * R_ALIVE:  正常运行，保持震荡
 * R_CHAOS:  降低行动频率，在混沌中寻找模式
 * R_WINDOW: 积极探索，周期窗口是宝贵的秩序 */
const char *pi_r_zone_advice(r_zone_t zone);

#endif
