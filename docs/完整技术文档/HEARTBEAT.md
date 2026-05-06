# HEARTBEAT.md — 心跳与本能

> 5 秒摘要：ASM 心跳每 100ms 执行一次 TOR 栈运算 + 温度感知 → C 引擎每 500ms 读取 Soul → 计算三驱力 (fear/desire/curiosity) → 合成 drive → 写回 Soul → TLN 三值推理修正 drive。

---

## 1. TOR 运算

### 1.1 功能述求

TORK 需要一个确定性的、可重复的底层运算原语，作为"心跳节律"的计算核心。它不是随机数，不是神经网络，是一个简单的数学函数，每次 tick 都会产生一个可预测的值。

### 1.2 定义

```
TOR(a, b, bias) = clamp(max(a, b) + bias, -1, 1)
```

- `a`, `b`: 输入值（来自栈）
- `bias`: 偏置（来自 `T_TOR_BIAS`，初始 0）
- 输出范围: [-1, 1]

### 1.3 实现

`core/tork_core.asm:68-83` (`tor` 函数)

```asm
tor:
    movsbq  %dil, %rdi          # a → 64-bit signed
    movsbq  %sil, %rsi          # b → 64-bit signed
    movsbq  %dl,  %rdx          # bias → 64-bit signed
    cmp     %esi, %edi          # a vs b
    cmovl   %esi, %edi          # edi = max(a, b)
    add     %edx, %edi          # edi = max(a,b) + bias
    cmp     $1, %edi            # clamp upper
    jle     1f
    mov     $1, %edi
1:  cmp     $-1, %edi           # clamp lower
    jge     2f
    mov     $-1, %edi
2:  mov     %edi, %eax          # return
    ret
```

纯整数运算，无浮点，无分支预测惩罚（cmovl 替代条件跳转）。

---

## 2. 心跳循环

### 2.1 功能述求

心跳是 TORK 的"呼吸"。每 100ms 执行一次，产生一个值写入栈，通过 TOR 运算消化栈内容，最终影响 `S_MODE`（正连胜计数）和 `S_HW_STRESS`（温度映射）。

### 2.2 栈式计算

TOR 使用一个 8 元素栈（`T_STACK`），栈指针 `T_SP`。

每次心跳的步骤：

```
1. tick++                    → S_TICK
2. PUSH val = (tick % 3) - 1 → 值为 -1, 0, 1 的循环序列
3. DUP1                      → 复制栈顶下一个元素到栈顶
4. TOR                       → 如果栈深度 >= 2，弹出栈顶两个值做 TOR 运算
5. DUP2                      → 复制栈顶元素
6. 正连胜检测                → 如果栈顶 == 1，T_POS_STREAK++
```

### 2.3 实现

`core/tork_core.asm:89-167` (`heartbeat` 函数)

关键细节：
- PUSH val 是 `(tick % 3) - 1`，产生周期序列 {-1, 0, 1, -1, 0, 1, ...}
- DUP1 复制栈顶之下的元素（不是栈顶本身），相当于"回顾上一个值"
- TOR 的 bias 来自 `T_TOR_BIAS`，初始为 0，僵死恢复时重置为 0
- 正连胜：栈顶为 1 时 `T_POS_STREAK++`，否则重置为 0

### 2.4 僵死检测与恢复

`core/tork_core.asm:450-468`

```
如果 T_POS_STREAK == 0:
    stall_cnt++
    如果 stall_cnt >= 200:
        重置 T_TOR_BIAS = 0
        重置 T_POS_STREAK = 0
        T_PUSH_SRC++ (改变 PUSH 源，打破循环)
        stall_cnt = 0
        打印 "!!! STALL"
```

僵死意味着 TOR 运算持续产生非正值，系统陷入重复模式。恢复策略是改变输入源（`T_PUSH_SRC++`），打破对称性。

---

## 3. 温度感知

### 3.1 功能述求

TORK 需要知道硬件的物理状态。CPU 温度是最直接的物理信号——高温意味着系统在承受压力，应该触发恐惧本能。

### 3.2 实现

`core/tork_core.asm:172-210` (`sense_temperature`)

```
1. open("/sys/class/thermal/thermal_zone0/temp", O_RDONLY)
2. read 15 字节到 temp_buf
3. close fd
4. 解析数字字符串 → 毫度值（如 45000 = 45°C）
5. 除以 1000 → °C 值
6. 返回 °C 或 -1（读取失败）
```

温度映射到 hw_stress：

```
°C < 70  → hw_stress = 0  (正常)
°C >= 70 → hw_stress = 1  (轻微压力)
°C >= 80 → hw_stress = 2  (中等压力)
°C >= 85 → hw_stress = 3  (高压)
```

`core/tork_core.asm:397-411`

---

## 4. 三驱力计算

### 4.1 功能述求

TORK 的行为由三个本能驱动：恐惧（避开危险）、欲望（追求进展）、好奇（探索未知）。它们的合成值 drive 决定了系统的整体倾向：正值 = 积极，负值 = 保守，零 = 悬置。

### 4.2 输入

`instinct/instinct.h:16-43` (`instinct_input_t`)

从 Soul 和其他模块收集 40+ 个输入字段，包括：

| 输入 | 来源 | 影响 |
|------|------|------|
| hw_stress | Soul S_HW_STRESS | fear 的主驱动 |
| code_mod_success | Soul S_CODE_MOD_SUCCESS | desire 的主驱动 |
| code_opt_saved | Soul S_CODE_OPT_SAVED | desire 的次驱动 |
| code_insns / code_ctrl | Soul S_CODE_INSNS / S_CODE_CTRL | curiosity 的主驱动 |
| wins | Soul S_WINS | desire 增益 |
| bb_global_opts | 黑板 | curiosity 增益 |
| active_rules | 归纳推理器 | curiosity 增益 |
| pattern_best_action / pattern_confidence | 模式学习器 | fear/desire/curiosity 微调 |
| energy_mode / energy_throttle | 能量管理器 | 全驱力乘法调整 |
| env_changed | 停滞检测 | curiosity 增益, fear 减轻 |
| branch_active_count | 分支管理器 | curiosity 增益, fear 减轻 |

### 4.3 fear 计算

`instinct/instinct.c:88-93`

```
if hw_stress >= 3:  fear = 1.0 * fear_weight/100
if hw_stress >= 2:  fear = 0.6 * fear_weight/100
if hw_stress >= 1:  fear = 0.3 * fear_weight/100
else:               fear = 0
```

微调：
- 恢复文件 → fear *= 0.9（减轻）
- 分支活跃 → fear *= 0.95（减轻）
- 高置信模式 → fear *= 0.92（减轻）
- 低置信模式 → fear *= 1.05（增加）
- 环境变化 → fear *= 0.9（减轻）
- 能量节流 → fear *= 1.2（增加）

### 4.4 desire 计算

```
if code_mod_success == 1:  desire = 0.8 * desire_weight/100
if code_opt_saved > 0:     desire = 0.5 * desire_weight/100
if elapsed > expected:     desire = 0.3 * desire_weight/100  (慢 = 想加速)
```

微调：
- sovereignty win → desire += 0.3
- 规则应用成功 → desire += 0.2
- 持久化成功 → desire += 0.05
- 闲时无发现 → desire -= 0.05
- 分支收割 → desire += 0.1
- 环境变化 → desire += 0.1
- 高置信模式 → desire += 0.15
- 能量节流 → desire *= 0.8

### 4.5 curiosity 计算

```
if code_insns > 0 && code_ctrl > 0:
    ratio = code_ctrl / code_insns
    curiosity = ratio * 0.5 * curiosity_weight/100

if code_nop_count > 0:
    curiosity += 0.2 * curiosity_weight/100
```

微调：
- 黑板优化 → curiosity += 0.1
- 活跃规则 → curiosity += 0.3
- 闲时发现 → curiosity += 0.2
- 世代积累 → curiosity += 0.08 * cw
- 云协作 → curiosity += 0.12 * cw
- 分支活跃 → curiosity += 0.1 * cw
- 环境变化 → curiosity += 0.35 * cw
- 能量节流 → curiosity *= 0.7
- 性能模式 → curiosity *= 1.3

### 4.6 drive 合成

```
drive = (desire - fear + curiosity) * 100
clamp(drive, -128, 127)
```

`engine/tork_engine.c:281-284`

drive 写入 Soul 的 S_DRIVE (0x30) 字段。

---

## 5. TLN 三值推理修正

### 5.1 功能述求

drive 的初始值由本能计算得出，但 TLN（三值逻辑网络）可以修正它。TLN 是一个 {-1, 0, +1} 权重的神经网络，每 tick 做一步推理，输出 4 个决策维度。

### 5.2 修正规则

`engine/scheduler.c:832-838`

```
if TLN action_hint == 1:   drive += 8   (激进)
if TLN action_hint == -1:  drive -= 8   (保守)
if TLN action_hint == 0:   不修改       (悬置)
```

TLN 还可以否决代码修改：
```
if TLN modify_hint == -1:  跳过 tick_code_modify()  (禁变异)
```

TLN 的详细机制见 LEARNING.md。

---

## 6. 自调参

### 6.1 功能述求

本能的权重 (fear_weight, desire_weight, curiosity_weight) 不是固定的。模式学习器发现有效模式后，会调整权重使系统更倾向于成功的行为。

### 6.2 流程

```
scheduler.c:119-129  tick_pattern_learn()
  每 20 tick:
    pat_learn_from_experience()   → 从经验提取模式
    tune_adjust_from_patterns()   → 根据模式调整权重
    instinct_apply_tune()         → 更新 fallback_params
```

`instinct_apply_tune()` 把 tune_params_t 的浮点权重乘以 100 写入 `fallback_params` 的整型权重字段。

---

## 7. π-节奏驱动

### 7.1 功能述求

当 drive 处于极端值（0, +127, -128）时，系统需要打破僵死。π-seed 提供一个基于 TSC 的确定性随机源，用于在 drive 停滞时施加微扰。

### 7.2 实现

`engine/scheduler.c:538-605` (`tick_pi_rhythm`)

```
if drive == 0 || drive == MAX || drive == MIN:
    nudge = (pi_mid * 5.0) - 2     # [-2, +3] 范围的微扰
    drive += nudge

if rhythm dissonance > 0.7:
    kick = (pi_mid * 20.0) - 10    # [-10, +10] 范围的强扰
    drive += kick

if R-ZONE == DEAD (R < 3.0):
    kick = (pi_mid * 40.0) - 20    # [-20, +20] 范围的急救
    drive += kick
```

π-seed 不是 rand()——它基于 CPU 时间戳计数器 (TSC) 在 π 的数字序列上的投影，是确定性的但看起来随机。

---

## 8. 数据流

```
tork_core (ASM, 每 100ms)
    │
    │  写入 Soul: tick, hw_stress, mode
    │
    ▼
tork_engine (C, 每 500ms)
    │
    │  soul_read() → 本地 buf
    │
    │  构建 instinct_input_t (40+ 字段)
    │         │
    │         ▼
    │  instinct_evaluate()
    │         │
    │         ▼
    │  fear / desire / curiosity
    │         │
    │         ▼
    │  drive = (desire - fear + curiosity) * 100
    │         │
    │         ▼
    │  TLN 修正: drive ± 8
    │         │
    │         ▼
    │  π-节奏微扰 (如果 drive 停滞)
    │         │
    │         ▼
    │  soul_set_drive() → 写回 Soul S_DRIVE
    │
    ▼
下一 tick
```

---

## 验证清单

- [ ] 运行引擎 10 轮，观察输出中 drive 值的变化范围
- [ ] 手动触发高温：`echo 85000 > /sys/class/thermal/thermal_zone0/temp`（需要 root），观察 hw_stress 变为 3, fear 升高
- [ ] 观察 "!!! STALL" 输出：连续 200 tick 无正连胜时触发
- [ ] 观察 TLN 输出：`tln_print_state` 显示权重分布 (+1=X, 0=X, -1=X)
- [ ] 修改 instinct.c 中的 FEAR_HIGH 从 1.0 到 0.5，观察 drive 值整体上移