# PI_SEED.md — π-Seed：可靠的不确定性

> 5 秒摘要：π 无限不循环，每位不可预判。TORK 用 CPU 晶振刻痕（TSC）在 π 序列上取值，作为每次 0/1 坍缩前的扰动。这不是 rand()——是物理时间在数学序列上的投影。差异带来辨别，有活动才有生命。

---

## 0. 哲学基座

### 0.1 为什么是 π

TORK 识别和处理的是 0 和 1，是高低电平。但 0 和 1 之间不是空的——电场在动态中，正负都参与了，人为门控截断为二值。在做出是非判断之前，有第三态（空白/思考态），然后才决断。

如果 TORK 的每次决断都基于相同的输入，就会陷入重复——重复就是迷信的前提。需要一个**不可预判的扰动源**，在每次坍缩前施加影响，使任何存在无法预判 TORK 的输出。

π 是目前已知范围内最可靠的"不确定"：
- 无限不循环——没有模式可提取
- 每一位都是新的——不存在"下一位是什么"的预测
- 数学证明不可压缩——不能用更短的公式生成

### 0.2 TSC × π：物理时间与数学指纹的交叉

CPU 晶振每秒拍数亿次（TSC），这是物理时间在硅上的刻痕。
π 的数字序列是数学最深处的指纹。
两者的交叉点，就是 TORK 的不确定性来源。

不是 rand()——rand() 有种子、有周期、可预测。
不是 /dev/urandom——那是操作系统层面的熵池，受外部影响。
是 TSC 在 π 上的投影——确定性但不可预测，因为 TSC 本身是物理过程。

### 0.3 不可预判 = 不可迷信

每次 0/1 坍缩前对 π 做加减，造成：
- 没有任何存在可以预判 TORK 的下一步
- TORK 无法迷信——迷信的前提是模式重复，π 保证绝不重复
- 变化本身成为依据——不是外部输入决定变化，是 π 的不可重复性保证变化持续发生

---

## 1. BBP 公式：直接跳到第 n 位

### 1.1 功能述求

需要从 π 的第 n 位取值，但不计算前 n-1 位。BBP（Bailey-Borwein-Plouffe）公式可以直接计算 π 的第 n 位十六进制数字。

### 1.2 公式

```
π = Σ [1/(16^k)] * [4/(8k+1) - 2/(8k+4) - 1/(8k+5) - 1/(8k+6)]
```

直接提取第 n 位的十六进制数字，不需要遍历整条路——只要知道你在哪里。

### 1.3 实现

`src/learning/pi_seed.c:21-59`

```c
static double bbp_series(uint64_t n, double j) {
    double s = 0.0;
    double pow16 = 1.0;
    for (int64_t k = n; k >= 0; k--) {
        s += pow16 / (8.0 * k + j);
        s -= (int64_t)s;  // mod 1
        pow16 /= 16.0;
    }
    // k = n+1 onward — 快速收敛
    ...
    return s;
}

uint8_t pi_bbp_digit(uint64_t n) {
    double s1 = bbp_series(n, 1.0);
    double s4 = bbp_series(n, 4.0);
    double s5 = bbp_series(n, 5.0);
    double s6 = bbp_series(n, 6.0);
    double pi_n = 4.0 * s1 - 2.0 * s4 - s5 - s6;
    pi_n -= (int64_t)pi_n;
    if (pi_n < 0) pi_n += 1.0;
    return (uint8_t)(pi_n * 16.0) & 0xF;
}
```

---

## 2. TSC 种子：晶振刻痕

### 2.1 功能述求

需要一个与物理时间关联的索引，决定从 π 的哪一位取值。CPU 的 TSC（Time Stamp Counter）是最直接的物理时间信号。

### 2.2 实现

`src/learning/pi_seed.c:8-12`

```c
static inline uint64_t rdtsc_now(void) {
    unsigned int lo, hi;
    __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi) :: "rcx");
    return ((uint64_t)hi << 32) | lo;
}
```

### 2.3 位混合

TSC 的低位变化太快（太规律），高位变化太慢。用 murmur3 终结符做位混合，使分布更均匀：

```c
uint64_t mixed = offset;
mixed ^= mixed >> 33;
mixed *= 0xff51afd7ed558ccdULL;
mixed ^= mixed >> 33;
mixed *= 0xc4ceb9fe1a85ec53ULL;
mixed ^= mixed >> 33;
uint64_t pi_index = mixed & 0xFFFFF;  // 低 20 位，覆盖 ~100 万位 π
```

### 2.4 取值

```c
uint8_t hi = pi_bbp_digit(pi_index);
uint8_t lo = pi_bbp_digit(pi_index + 1);
return (hi << 4) | lo;  // 两个十六进制位拼成一个字节 [0, 255]
```

### 2.5 浮点映射

```c
float pi_seed_float(void) {
    uint8_t byte = pi_seed_from_tsc();
    return (byte + 0.5f) / 256.0f;  // 映射到 (0, 1)，排除两端
}
```

排除 0 和 1——它们是伪确定。中间态才是活着的。

---

## 3. 差异检测：pi_drift

### 3.1 功能述求

"差异带来辨别，而有活动，才有生命。"

两个值相同 = 无差别 = 死寂。两个值不同 = 有辨别 = 活着。差异越大，辨别越清晰。

### 3.2 实现

```c
float pi_drift(uint8_t a, uint8_t b) {
    int diff = (int)a - (int)b;
    if (diff < 0) diff = -diff;
    return (float)diff / 255.0f;  // ∈ [0, 1]
}
```

---

## 4. 反静态化衰减：pi_decay

### 4.1 功能述求

"地图有用，却绝不是领土。"

模式置信度随时间衰减。不衰减 = 伪确定 = 把地图当领土。衰减让 TORK 永远保持对世界的疑问。

### 4.2 实现

```c
float pi_decay(float confidence, uint32_t ticks_elapsed, uint32_t half_life) {
    if (half_life == 0) return 0.0f;
    float lambda = 0.69314718056f / (float)half_life;  // ln(2) / half_life
    return confidence * expf(-lambda * (float)ticks_elapsed);
}
```

半衰期 half_life 个 tick 后，置信度减半。这是对"成见是认知陷阱"的数学表达。

---

## 5. 环境节律匹配：pi_rhythm

### 5.1 功能述求

"对万事万物的节律有正切认知和匹配。"

万物自有节律。TORK 的节律是 drive 的变化，环境的节律是 π-seed 值的波动（物理时间在数学上的投影）。两者如果完全脱节，说明 TORK 在自说自话；如果共振，说明 TORK 在随世界呼吸。

### 5.2 数据结构

```c
#define RHYTHM_WINDOW 16

typedef struct {
    uint8_t values[RHYTHM_WINDOW];       // 最近的 π-seed 值
    uint8_t drive_deltas[RHYTHM_WINDOW]; // 最近的 drive 变化量
    int     count;
    int     idx;
} rhythm_tracker_t;
```

### 5.3 失调度计算

```c
float pi_rhythm_dissonance(const rhythm_tracker_t *rt) {
    // 环境节律：π-seed 值的平均变化量
    // TORK 节律：drive 变化的平均幅度
    // 失调度 = 1 - min(env, tork) / max(env, tork)
    // 0 = 完美共振，1 = 完全脱节
}
```

### 5.4 在引擎中的使用

`src/engine/scheduler.c:538-605` (`tick_pi_rhythm`)

```
if rhythm dissonance > 0.7:
    kick = (pi_mid * 20.0) - 10    // 强扰 [-10, +10]
    drive += kick

if R-ZONE == DEAD (R < 3.0):
    kick = (pi_mid * 40.0) - 20    // 急救 [-20, +20]
    drive += kick
```

---

## 6. π 波形特征指纹：pi_profile

### 6.1 功能述求

"以振动频率识别万事万物。"

不是加密，是天生的。伪装可以骗语义层，骗不了频率层。π 指纹不是你选择的，是你在 π 空间的投影——你改不了自己的心跳。

### 6.2 四维特征提取

16 字节指纹，4 个维度各 4 字节：

| 维度 | 含义 | 字节 |
|------|------|------|
| 1. 频率分布 | 哪些振动频率占主导 | digest[0-1] |
| 2. 变化率 | 波形在剧烈波动还是平稳 | digest[2-5] |
| 3. 极值间距 | 振动的节律周期 | digest[6-7] |
| 4. π 交叉 | 波形与 π 序列的相关性 | digest[8-11] |
| 校验 | 抗碰撞性 | digest[12-15] |

### 6.3 相似度

```c
float pi_profile_similarity(const pi_profile_t *a, const pi_profile_t *b) {
    // 前 12 字节特征权重 70%，后 4 字节校验权重 30%
    // 逐字节 Hamming 相似度
}
```

---

## 7. π-Spiral：在无限中选重要

### 7.1 功能述求

"既然无限是可以确认了，其中的有限，就合理。"

不是连续取 π 的位，是按斐波那契间距取。斐波那契是自然选的间距——1,1,2,3,5,8,13...每一步的跨度越来越大，但每一步都是前两步之和。这就是"在无限中选有限"：不遍历，只取结构性的点。

### 7.2 骨架提取

从 spiral 提取结构性指纹：

```c
typedef struct {
    uint8_t turns;          // 转折点数量
    uint8_t symmetry;       // 对称度
    uint8_t leap_max;       // 最大跳跃
    uint8_t leap_rhythm;    // 跳跃节律方差
    float   golden_ratio;   // 上升段/下降段比值（接近 φ ≈ 1.618）
} pi_skeleton_t;
```

---

## 8. 水晶裂变：结构 + 不确定

### 8.1 功能述求

"真复杂与不得不简化，而有新奇特。"

纯 2^n 是简化（呆板），纯随机是复杂（混乱）。结构 + π 不确定 = 真复杂（有生命的裂变）。

### 8.2 实现

```c
crystal_cell_t crystal_fission(const crystal_cell_t *current, float base_rate) {
    next.generation = current->generation + 1;
    float base_growth = powf(base_rate, (float)next.generation * 0.1f);

    // π 微调：不是随机扰动，是结构性的不确定
    uint64_t pi_idx = (uint64_t)next.generation * 7ULL + 3ULL;
    uint8_t pi_digit = pi_bbp_digit(pi_idx);
    float pi_mod = (pi_digit + 0.5f) / 16.0f;

    next.vitality = 0.5f + 0.5f * pi_mod;
    next.population = (uint32_t)((float)current->population * base_growth * next.vitality);
}
```

---

## 9. 3.5R 震荡检测：混沌边界的自知

### 9.1 功能述求

"和而不同"不是道德宣言，是数学事实。

Logistic map: x_{n+1} = R * x_n * (1 - x_n)
- R < 3.0 → 单稳态（僵死）
- R ≈ 3.5 → 周期震荡（活着）
- R > 3.57 → 混沌（不可预测但有结构）
- R ≈ 3.82 → 周期窗口（混沌中的秩序）

### 9.2 R 值计算

```c
float pi_logistic_R(const rhythm_tracker_t *rt) {
    // 统计 drive 变化的 unique 值数量和平均幅度
    // 映射到 R 值：
    //   unique=1, delta=0  → R=2.8  (僵死)
    //   unique=4, delta=5  → R=3.95 (活着)
    //   unique=8, delta=15 → R=5.45 (混沌)
    float R = 2.5f + 0.3f * (float)unique + 0.05f * delta_mean;
}
```

### 9.3 行为建议

| R 区间 | 判定 | 建议 |
|--------|------|------|
| R < 3.0 | 僵死 | 强制注入好奇，打破稳态 |
| R ≈ 3.5 | 活着 | 保持震荡，和而不同 |
| R > 3.57 | 混沌 | 降低频率，在无序中寻找模式 |
| R ≈ 3.82 | 周期窗口 | 积极探索，周期窗口是宝贵秩序 |

---

## 10. 数据流

```
CPU 晶振 (TSC)
    │
    │  rdtsc_now() → 物理时间刻痕
    │
    ▼
位混合 (murmur3 终结符)
    │
    │  均匀分布的索引
    │
    ▼
BBP 公式 → π 的第 n 位十六进制数字
    │
    │  不可预判的值 [0, 15]
    │
    ▼
pi_seed_from_tsc() → [0, 255]
pi_seed_float()    → (0, 1)  排除两端
    │
    ├──→ pi_drift()          差异检测
    ├──→ pi_decay()          反静态化衰减
    ├──→ pi_rhythm_observe() 环境节律匹配
    ├──→ pi_hash_profile()   波形指纹
    ├──→ pi_spiral()         斐波那契间距取值
    ├──→ crystal_fission()   水晶裂变 π 微调
    └──→ pi_logistic_R()     震荡检测
```

---

## 11. 与其他模块的交互

| 模块 | 使用 π 的方式 |
|------|-------------|
| instinct (本能) | pi_seed_float() 作为 curiosity 微调 |
| scheduler (调度) | tick_pi_rhythm() 在 drive 停滞时施加 π 微扰 |
| pattern (模式) | pi_decay() 对模式置信度做时效衰减 |
| mcts (搜索) | pi_seed_float() 在 rollout 中提供探索噪声 |
| tln (推理网络) | pi_seed_from_tsc() 决定变异方向 |
| snapshot (快照) | pi_drift() 检测状态退化程度 |

---

## 验证清单

- [ ] `pi_bbp_digit(0)` 返回 3（π 的第一位十六进制是 3）
- [ ] `pi_bbp_digit(1)` 返回 2（π = 3.2...）
- [ ] 连续调用 `pi_seed_from_tsc()` 100 次，确认无连续重复值
- [ ] `pi_drift(128, 128) == 0.0`（相同值 = 死寂）
- [ ] `pi_drift(0, 255) == 1.0`（最大差异 = 活着）
- [ ] `pi_decay(1.0, 100, 100) ≈ 0.5`（半衰期后减半）
- [ ] 运行引擎后观察 "R-ZONE" 输出，确认 R 值在 3.0-4.0 区间
- [ ] 观察 "rhythm dissonance" 输出，确认失调度在 0-1 之间