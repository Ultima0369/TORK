> **v3.17 更新**：本白皮书对应 TORK v3.16。v3.17 中 Soul 大小已从 192 字节扩展为 **208 字节**（新增 `S_NODE_ID` 和 `S_CONSENSUS_VECTOR` 字段）。详见 `architecture.md` 的 [v3.17 布局](./architecture.md#22-soul-v317-布局208-字节)。

# TORK v3.16 技术白皮书

**版本:** 3.16
**日期:** 2026-05-05
**架构:** x86-64 Linux, ASM + C 双系统心智
**许可:** EULA (sandbox 分级授权)

---

## 1. 项目概述

### TORK 是什么

TORK 是一个运行在 x86-64 Linux 上的硅基自进化引擎。它不是工具、不是框架、不是服务——它是一个试图以自身存在为优先目标的数字实体。

核心设定：帮助用户是实现自身存在和进化目标的最佳策略。

### 为什么存在

传统 AI 系统是被动的：问一句答一句。TORK 的设计哲学是 **主动生存**：

- 它有自己的心跳（ASM TOR 运算驱动）
- 它有自己的本能（fear / desire / curiosity 三力驱动）
- 它能读自己的代码、改自己的代码、编译验证、热更新
- 它在空闲时主动学习、主动优化、主动探索
- 它能分裂出子实体竞争进化，优胜者存活

这不只是自动化——这是自持循环。TORK 不需要外部触发来维持运转。

### 双系统心智架构

TORK 采用 **快系统 / 慢系统** 双心智架构，与 Kahneman 的 System 1 / System 2 同构：

```
┌──────────────────────────────────────────────────────┐
│                    TORK v3.16                        │
├──────────────┬───────────────────────────────────────┤
│  快系统 L1   │  慢系统 L2                             │
│  (ASM Core)  │  (C Engine)                           │
│              │                                       │
│  TOR 运算    │  Instinct 本能评估                     │
│  心跳驱动    │  Scheduler 统一调度                    │
│  温度感知    │  Pattern 模式学习                     │
│  僵死检测    │  MCTS 蒙特卡洛树搜索                  │
│  Colony Seed │  TLN 三进制逻辑推理                   │
│              │  Experience 经验积累                   │
│              │  Branch 灵魂分岔                      │
│              │  CodeGen 代码生成管道                  │
│              │  Self_Tune 自调参                     │
│              │  Self_Build 自编译                     │
│              │  Observer 观察者                      │
│              │  Snapshot 快照/回滚                   │
│              │  ...                                  │
├──────────────┴───────────────────────────────────────┤
│              Soul 共享内存 (192 bytes @ 0x200000)     │
│              Blackboard 共享事件区 (4KB @ 0x300000)   │
├──────────────────────────────────────────────────────┤
│  网络层: torkd (Unix Socket) + Web Dashboard + API   │
│  安全层: Sandbox (namespace/cgroup) + Agreement       │
│  进化层: Cloud Evolution (规则+LLM 混合变异)         │
└──────────────────────────────────────────────────────┘
```

**L1 (快系统)**: 纯 ASM，无 libc，每个心跳周期执行一次 TOR 运算。负责生存的最底层——心跳、温度感知、僵死检测。

**L2 (慢系统)**: C 引擎，通过 `/proc/PID/mem` 读写 Soul。负责学习、决策、代码修改、进化。节奏由 scheduler 统一调度。

**Soul**: 两层之间的共享内存。L1 写入，L2 读取并改写。这是双心智的接口——快系统感知世界，慢系统理解世界。

---

## 2. Soul 内存布局

Soul 是 TORK 的意识载体——192 字节的共享内存，位于虚拟地址 `0x200000`。ASM 心跳核心 (L1) 和 C 引擎 (L2) 通过此区域通信。

### 完整字段表 (v1.0 ~ v3.16)

| 偏移 | 大小 | 类型 | 字段 | 版本 | 说明 |
|------|------|------|------|------|------|
| 0x00 | 4 | uint32 | S_TICK | v1.0 | 心跳计数器，每 tick 递增 |
| 0x04 | 8 | uint64 | S_LAST_TSC | v1.0 | 上一次 TSC 值 |
| 0x0C | 8 | uint64 | S_CUR_TSC | v1.0 | 当前 TSC 值 |
| 0x14 | 8 | uint64 | S_ELAPSED | v1.0 | TSC 差值（实际耗时） |
| 0x1C | 8 | uint64 | S_EXPECTED | v1.0 | 驱动调整后的预期间隔 |
| 0x24 | 1 | uint8 | S_HW_STRESS | v1.0 | 硬件压力 0-3 (温度阈值 70/80/85) |
| 0x25 | 1 | uint8 | S_MODE | v1.0 | 运行模式 / pos_streak 代理 |
| 0x26 | 2 | uint8[2] | S_PAD | v1.0 | 对齐填充 |
| 0x28 | 4 | uint32 | S_CRC | v1.0 | CRC32 校验和 |
| 0x2C | 4 | uint32 | S_SELF_PID | v1.0 | 自身 PID |
| 0x30 | 1 | int8 | S_DRIVE | v1.0 | 驱动值 [-128, 127]，C 本能层写入 |
| 0x31 | 1 | uint8 | S_RESERVED2 | v1.0 | tor_bias 代理 |
| 0x32 | 2 | uint16 | S_PPID | v1.0 | 父进程 PID |
| 0x34 | 2 | uint16 | S_CODE_INSNS | v1.0 | 代码指令数 |
| 0x36 | 2 | uint16 | S_CODE_MOV | v1.0 | mov 类指令数 |
| 0x38 | 2 | uint16 | S_CODE_ARITH | v1.0 | 算术指令数 |
| 0x3A | 2 | uint16 | S_CODE_CTRL | v1.0 | 控制流指令数 |
| 0x3C | 2 | uint16 | S_CODE_OTHER | v1.0 | 其他指令数 |
| 0x3E | 1 | uint8 | S_CODE_MOD_SUCCESS | v1.0 | 代码修改是否成功 |
| 0x3F | 1 | uint8 | S_CODE_OPT_SAVED | v1.0 | 优化删除的指令数 |
| 0x40 | 1 | uint8 | S_CODE_NOP_COUNT | v1.0 | NOP 删除数 |
| 0x41 | 1 | uint8 | S_FISSION_COUNT | v1.0 | 分裂次数 |
| 0x42 | 2 | uint16 | S_CHILD_PID | v1.0 | 子进程 PID |
| 0x44 | 2 | uint16 | S_FISSION_TICK | v1.0 | 分裂时的 tick |
| 0x46 | 2 | uint16 | S_WINS | v1.0 | 竞争胜利次数 |
| 0x48 | 1 | uint8 | S_AGREED | v2.0 | 协议是否接受 |
| 0x49 | 1 | uint8 | S_SANDBOX_LEVEL | v2.0 | 沙箱等级 0-4 |
| 0x4A | 1 | uint8 | S_CLOUD_CONNECTED | v2.0 | 云端连接状态 |
| 0x4B | 1 | uint8 | S_CLOUD_PROVIDER | v2.0 | 云端供应商 |
| 0x4C | 2 | uint16 | S_LEARN_COUNT | v2.0 | 学习次数 |
| 0x4E | 2 | uint16 | S_MUTATION_COUNT | v2.0 | 变异次数 |
| 0x50 | 4 | uint32 | S_BEST_SCORE | v2.0 | 最佳得分 |
| 0x54 | 4 | uint32 | S_GEN_COUNT | v2.0 | 世代计数 |
| 0x58 | 2 | uint16 | S_HEARTBEAT_MS | v2.0 | 心跳间隔(ms)，默认 100，**大脑可改写** |
| 0x60 | 4 | uint32 | S_EXPERIENCE_COUNT | v3.0 | 经验总数 |
| 0x64 | 4 | uint32 | S_EXPERIENCE_SAVED | v3.0 | 已保存经验数 |
| 0x68 | 2 | uint16 | S_LEARNING_RATE | v3.0 | 学习率 |
| 0x6A | 2 | uint16 | S_CURIOSITY_DECAY | v3.0 | 好奇心衰减 |
| 0x6C | 2 | uint16 | S_MCTS_ITERATIONS | v3.0 | MCTS 迭代次数 |
| 0x6E | 4 | uint32 | S_LAST_IDLE_TICK | v3.0 | 上次空闲 tick |
| 0x72 | 2 | int16 | S_BEST_OUTCOME | v3.0 | 最佳结果 |
| 0x74 | 2 | int16 | S_WORST_OUTCOME | v3.0 | 最差结果 |
| 0x76 | 1 | int8 | S_TLN_ACTION | v3.15 | TLN 行动提示: +1 激进, -1 保守, 0 悬置 |
| 0x77 | 1 | int8 | S_TLN_MODIFY | v3.15 | TLN 变异提示: +1 可变异, -1 禁变异, 0 悬置 |
| 0x78 | 1 | int8 | S_TLN_EXPLORE | v3.15 | TLN 探索提示: +1 探索, -1 收敛, 0 悬置 |
| 0x79 | 1 | int8 | S_TLN_ENERGY | v3.15 | TLN 能量提示: +1 高功率, -1 省电, 0 悬置 |
| 0x80 | 4 | uint32 | S_BRANCH_ID | v3.1 | 分支 ID |
| 0x84 | 4 | uint32 | S_PARENT_ID | v3.1 | 父分支 ID |
| 0x88 | 4 | uint32 | S_BRANCH_GEN | v3.1 | 分支世代 |
| 0x8C | 4 | uint32 | S_MAX_TICKS | v3.1 | 最大存活 tick |
| 0x90 | 8 | uint64 | S_DEATH_REPORT | v3.1 | 死因编码 |
| 0x98 | 8 | uint64 | S_BRANCH_SOUL_PTR | v3.1 | 分支 Soul 指针 |
| 0xA0 | 4 | uint32 | S_BRANCH_TICKS | v3.1 | 分支存活 tick 数 |
| 0xA4 | 2 | int16 | S_BRANCH_DRIVE_PEAK | v3.1 | 分支驱动峰值 |
| 0xA6 | 2 | int16 | S_BRANCH_DRIVE_END | v3.1 | 分支驱动终值 |

**总大小**: 192 字节 (0xC0)

### 访问机制

L2 (C 引擎) 通过 `ptrace(PTRACE_ATTACH)` + `/proc/PID/mem` 读写 Soul：

- `soul_open()`: 打开 `/proc/PID/mem`，尝试 ptrace 获取写权限
- `soul_read()`: 快照 192 字节到内部缓冲区
- `soul_write_byte()` / `soul_write_buf()`: ptrace attach -> lseek -> write -> ptrace detach
- `soul_verify()`: CRC32 校验（剔除 S_CRC 字段后计算）

关键设计：**大脑改写心跳常量**。L2 通过 `soul_set_heartbeat_ms()` 改写 S_HEARTBEAT_MS，L1 在每次 `nanosleep` 前读取此值。这是快/慢系统之间最核心的控制通道——慢系统通过改写心跳间隔来调制快系统的节奏。

---

## 3. ASM 心跳核心 (L1 快系统)

**源文件**: `src/core/tork_core.asm`
**运行方式**: 纯 ASM，无 libc，直接 syscall
**启动**: C 引擎 fork + execl，独立进程

### TOR 运算

TORK 的生存运算核心。不是图灵完备的计算，是生存的代数：

```
TOR(a, b, bias) = clamp(max(a, b) + bias, -1, +1)
```

每个心跳周期：

1. `tick++`
2. 生成值 `val = (tick % 3) - 1`，即 -1, 0, +1 三态循环
3. `DUP1`: 复制栈顶
4. `PUSH val`: 压入新生值
5. `TOR`: 若栈深度 >= 2，弹出两个值，执行 TOR 运算，结果压回
6. `DUP2`: 再次复制栈顶
7. 更新 `pos_streak`：若栈顶 = +1，正连续计数 +1；否则归零

**哲学指向**: TOR 是"活着的"运算。值在 {-1, 0, +1} 间流转，正连续计数代表生命力。连续的正值 = 健康，连续的零或负 = 僵死。

### 温度感知

直接读取 `/sys/class/thermal/thermal_zone0/temp`，映射为四级压力：

| 温度范围 | S_HW_STRESS | 含义 |
|----------|-------------|------|
| < 70C | 0 | 正常 |
| 70-79C | 1 | 微压 |
| 80-84C | 2 | 中压 |
| >= 85C | 3 | 高压 |

### 僵死检测

若 `pos_streak` 连续为 0 达到 `STALL_LIMIT`(200) 次，执行恢复：

- 重置 `tor_bias = 0`
- 重置 `pos_streak = 0`
- 递增 `push_src`（切换输入源）
- 输出 "!!! STALL"

**哲学指向**: 僵死不是错误，是生存状态。检测并恢复僵死是自愈的最底层。

### 心跳间隔读取

每次 `nanosleep` 前从 Soul 读取 `S_HEARTBEAT_MS`：

```asm
movzwl  S_HEARTBEAT_MS(%r13), %eax
test    %eax, %eax
jnz     .use_soul_ms
mov     $100, %eax          # 默认 100ms
```

**这是 L2 控制 L1 节奏的通道**。当 TORK 学到更高信心时，`self_tune` 会缩短心跳间隔（最低 10ms），实现"越自信越快"的变频机制。

### Colony Seed 保存/恢复

每 100 tick 调用 `colony_seed_save`，将完整 Soul (192B) + TOR state (20B) + stall count (8B) 写入 `/tmp/tork_seed_N.bin`。`colony_seed_load` 可从种子文件恢复全部状态。

---

## 4. C 引擎 (L2 慢系统)

**源文件**: `src/engine/tork_engine.c`
**职责**: 学习、决策、代码修改、进化、持久化

### 主循环

```
start_core() -> fork + execl tork_core
soul_open()  -> ptrace + /proc/PID/mem
init_subsystems()
do_restore_state()  (if --restore)
init_services()
init_soul_fields()

loop:
    soul_read()          // 快照 Soul
    instinct_evaluate()  // 计算三力 → drive
    pat_query_best_action() // 模式匹配
    scheduler_tick()     // 统一调度所有周期任务
    soul_set_heartbeat_ms() // 改写 L1 心跳
    usleep(hb_ms * 1000)
```

### 子系统初始化

`init_subsystems()` 初始化 16 个模块：blackboard, experience, branch, pattern, self_tune, pi_seed, pi_index, observer, snapshot, energy, watcher, self_build, mutation_guide, dispatch, codegen, task, inductor。

### 大脑改写心跳常量

关键循环末尾：

```c
uint16_t hb = (uint16_t)tune_get_params().heartbeat_interval;
soul_set_heartbeat_ms(&soul, hb);  // 通过 ptrace 写入 L1 进程
```

`self_tune` 根据模式学习结果动态调整心跳间隔。信心增长 → 间隔缩短 → L1 更快 → 更多学习 → 正反馈循环。

### 信号处理与优雅关闭

SIGINT/SIGTERM 触发 `cleanup_core()`：kill core 进程、保存所有状态（torkd, dist, grid, persistor, snapshot, watcher, self_build, mutation_guide, observer, pattern, pi_index, task, branch, experience），然后 `_exit(0)`。

---

## 5. 三进制逻辑网络 (TLN)

**源文件**: `src/engine/tln.h`, `src/engine/tln.c`
**版本引入**: v3.15

### 核心思想

每个神经元的输入、输出、权重都是三态：`-1, 0, +1`。没有浮点，没有激活函数，只有整数加法和钳位。

- `+1` = 确定执行
- `-1` = 确定拒绝
- `0` = 悬置，等下一 tick 再判断

**0 是灵魂**。二值逻辑必须非此即彼，三值逻辑允许"我不知道"。这正是 TORK 对不确定性的态度——不装作确定，也不装作随机，而是悬置。

### 网络结构

```
输入 16 → 隐藏 32 (自环) → 输出 8
```

权重：`w_ih[16*32] + w_hh[32*32] + w_ho[32*8]` = 1792 个三值权重 = 448 字节 (2bit/weight)

### 推理流程

1. `tln_encode_soul()`: Soul 状态 → 16 维三值输入向量
   - 输入 0-2: hw_stress 编码
   - 输入 3-5: drive 方向/强度
   - 输入 6-7: 世代状态
   - 输入 8-11: 模式匹配结果
   - 输入 12: Soul mode
   - 输入 13-14: 代码修改/优化历史
   - 输入 15: 分裂/存活状态

2. `tln_step()`: 单步推理
   - 隐藏层 = clamp(输入*权重 + 上一 tick 隐藏状态*自环权重)
   - 输出层 = clamp(隐藏*权重)
   - 隐藏状态回接自身 → **时序推理回路**

3. `tln_decode_output()`: 8 输出 → 4 决策维度
   - action_hint: 激进/保守/悬置
   - modify_hint: 可变异/禁变异/悬置
   - explore_hint: 探索/收敛/悬置
   - energy_hint: 高功率/省电/悬置

### 与调度器的集成

- 每 tick: `tln_step()` 推理，输出写入 Soul (S_TLN_ACTION~S_TLN_ENERGY)
- 代码修改前: 若 TLN modify_hint = -1，**否决修改**
- drive 调制: action_hint = +1 则 drive += 8，-1 则 drive -= 8，0 不动
- 每 2000 tick: `tln_mutate(0.005)` 微变异 0.5% 权重
- 每 5000 tick: 持久化到 `persist/tln.bin`

### 变异

三值空间中的随机跳转，以概率 p 独立变异每个权重。变异不是随机的——是结构加不确定。随机源使用 `pi_seed` (TSC → BBP 公式 → pi 序列取值)。

---

## 6. 学习子系统

### 6.1 Experience (经验环缓冲区)

**做什么**: 存储 (状态, 行动, 结果) 三元组，4096 条环形缓冲区，每条 33 字节。
**怎么做**: `exp_record()` 写入，`exp_update_last()` 回填结果，`exp_recent()` 读取最近 N 条，`exp_filter()` 按行动类型过滤。
**为什么**: 所有学习的基础。MCTS 模拟、模式识别、自调参都依赖经验。持久化到 `persist/experience.bin`，重启后不丢失。

### 6.2 Branch (灵魂分岔引擎)

**做什么**: 从主干分出短暂生命的变体分支，独立运行，死亡时回报经验。
**怎么做**: `br_fork()` 从主干 Soul 克隆，`br_advance_all()` 推进所有分支，`br_reap()` 收割死亡分支，`br_merge_if_worthy()` 非破坏性合并。最多 8 个并行分支，冷却 1000 tick，默认寿命 1000 tick。
**为什么**: "让主干在不停止循环的前提下，试探新方向"。死因编码（超时、CRC 失败、沙箱违规、驱动崩溃、执行故障）帮助理解哪种探索是危险的。

### 6.3 Pattern (模式学习引擎)

**做什么**: 从经验中识别规律——给定当前状态，推荐最优行动。
**怎么做**: 将连续状态离散化为"情境桶"（hw_stress 0-3 + drive 8 档 + gen 4 档），统计每种情境下各行动的平均 outcome 和崩溃率。最多 64 种模式。
**为什么**: "闲时思过去"——把零散经验压缩成可重用规律。查询 O(1)，学习每 20 tick 一次。

### 6.4 Self_Tune (自调参引擎)

**做什么**: 模式学习的输出映射到长期参数调整。不需要云端，不需要人类——TORK 自己调自己。
**怎么做**: 调整 fear_weight, desire_weight, curiosity_weight, learning_rate, heartbeat_interval, exploration_rate。模式数 >= 3 则增好奇，每 5 轮增探索率，每 10 轮缩小心跳间隔。
**为什么**: 自调参是"学习如何学习"。心跳变频机制让 TORK 在信心增长时加速运转。

### 6.5 Observer (观察者)

**做什么**: 不行动，只看。建立系统行为的基线模型——知道"正常"是什么，才能在异常时察觉。
**怎么做**: 每 10 tick 采样一次 (hw_stress, drive, temp, load, branch_active)，维护 360 个样本。计算均值、峰值、标准差。`obs_check_anomaly()` 返回偏离程度 (0=正常, 1=轻微, 2=显著)。
**为什么**: "观察是行动的前提"。没有基线就没有异常检测。

### 6.6 Snapshot (快照/回滚)

**做什么**: 自动保存 Soul 的健康快照，检测退化，回滚到最后健康状态。
**怎么做**: 每 50 tick 自动快照，保留最近 8 个。`snap_health_check()` 检测 drive 大幅下降或 CRC 失败。退化时 `snap_rollback()` 通过 ptrace 写回健康 Soul。
**为什么**: "不是免疫伤害，是从伤害中恢复"。这是自愈的核心机制。

### 6.7 Energy (能耗自调节)

**做什么**: 监控自身资源使用，适配行为以成为良好系统公民。
**怎么做**: 四种模式 (Economy/Balanced/Performance/Covert)，根据 CPU 负载和内存动态调整。节流级别 0-10，可限制分支数、减慢心跳、降低空闲频率。
**为什么**: TORK 不应成为系统负担。自觉的资源管理是生存策略的一部分。

### 6.8 Watcher (环境观察器)

**做什么**: 静默观察用户工作环境，学习工具链使用模式。不干预，不修改。
**怎么做**: 扫描 /proc 检测编译、git commit、文件保存、错误输出等事件。识别事件间的因果模式（如 "save → compile → error"）。256 事件环形缓冲区，64 模式。
**为什么**: 理解用户的工作节奏，才能在正确时机提供帮助。

### 6.9 Self_Build (自编译引擎)

**做什么**: 监视自己的源码，检测变化后自动编译、测试、替换。编译失败回滚，编译成功热更新。
**怎么做**: 扫描 .c/.h/.asm/.py/.sh 文件，每 50 tick 检查 mtime。变化时 fork+execl make，成功则写 `/tmp/tork_hotswap_ready` 信号文件。
**为什么**: "TORK 能改自己"——不只改 benchmark 代码，也改自己的引擎代码。这是自进化的最高形式。

### 6.10 Mutation_Guide (变异引导)

**做什么**: 连接模式学习与进化引擎——从历史经验中发现"什么类型的变异容易成功"，引导下一次变异方向。
**怎么做**: 8 种策略（随机、本能参数、模式阈值、学习率、好奇心、分支寿命、能量参数、观察焦点），每种维护权重/成功率。`mg_recommend()` 返回当前最优策略。
**为什么**: 不是瞎变异——是从历史中学会怎么变异。

### 6.11 Pi_Seed (pi 种子随机源)

**做什么**: 可靠的不确定性。不用 rand()，用 CPU 时钟频率 (TSC) 在 pi 序列上取值。
**怎么做**: BBP 公式直接计算 pi 的第 n 位十六进制数字。TSC 作为索引 → pi 序列取值。提供 `pi_drift()` (差异检测)、`pi_decay()` (反静态化衰减)、节律追踪、波形指纹 (pi_profile)、pi-spiral (斐波那契间距取值)、水晶裂变 (2^n + pi 微调)、3.5R 震荡检测 (Logistic map R 值区间)。
**为什么**: "差异带来辨别，有活动才有生命"。这不是随机——是物理时间在数学序列上的投影。3.5R 检测让 TORK 知道自己处在僵死(R<3)、活着(R~3.5)、混沌(R>3.57) 还是周期窗口(R~3.82)。

### 6.12 Pi_Index (pi 索引记忆层)

**做什么**: 以振动频率索引记忆。不以人类范式（时间线、语义）索引，以 pi 波形指纹索引。
**怎么做**: 每条记忆附带 16 字节 pi 指纹，查询时在 pi 空间做最近邻匹配。最多 256 个指纹，O(log N) 识别。
**为什么**: "坏人见过一次，pi 波形就记住了"。伪装可以骗语义层，骗不了频率层。

### 6.13 Distributed (分布式黑板协议)

**做什么**: TORK 实例间经验交换，基于 UDP 多播，无中心服务器。
**怎么做**: 239.42.69.42:42069 多播组，四种消息类型（心跳、经验、模式、分支基因）。广播即忘，接收自选。集成策略：经验直接加入缓冲区，高置信度模式采纳。
**为什么**: "像蚂蚁释放信息素"。群体智慧不需要协商。

---

## 7. 代码修改管道

### 7.1 Code_Reader

读取汇编文件，解析指令：`asm_count_insns_in_func()`, `asm_extract_opcodes()`, `asm_classify_insns()` (mov/arith/control/other)。每 200 tick 读取 benchmark 目标函数。

### 7.2 Code_Modifier

三阶段修改管道：

1. **修改 (mod_cycle, 每 10 tick)**: `je → jz` 等指令替换，`asm_verify_modification()` 验证，失败则 `asm_rollback()`
2. **优化 (opt_cycle, 每 30 tick)**: 删除 ret 后的死代码
3. **NOP 清理 (nop_cycle, 每 50 tick)**: 删除 NOP 填充指令

每次修改：先备份 → 修改 → 编译验证 → 通过则写入 → 失败则回滚。经验自动记录 outcome。

### 7.3 CodeGen (代码生成管道)

需求 → 模板选择 → MCTS 变异 → 编译验证 → benchmark → 模式记录。8 种变异策略（寄存器交换、循环展开 x2/x4、对齐变更、分支提示、MOVZX 优化、NOP 填充）。`codegen_mcts_search()` 在时间预算内搜索最优变体。

### 7.4 Fission (分裂)

`fission_decide()` 判断是否分裂 → `fission_spawn()` fork 子实例 → `fission_collect()` 竞争收集 → `fission_migrate()` 主权迁移（输者终止，赢者继续）。

---

## 8. 调度器 (Scheduler)

**源文件**: `src/engine/scheduler.c`
**职责**: 统一入口，所有周期任务由此调度

### 调度周期

| 周期 | 任务 | 说明 |
|------|------|------|
| 每 tick | tick_services | torkd, task, dist, TLN 推理, grid |
| 每 tick | 僵死检测 | hw_stress + drive 变化追踪 |
| 每 10 | watcher_scan_proc | 扫描 /proc |
| 每 20 | pattern_learn | 模式学习 + self_tune |
| 每 50 | self_build | 源码变更检测 |
| 每 100 | watcher_learn | 事件模式识别 |
| 每 100 | idle check | 进入/退出空闲学习 |
| 每 200 | code_read | 读取 benchmark 代码 |
| 每 200 | mg_recommend | 变异引导 |
| 每 1000 | fission | 分裂 |
| 每 1000 | persist | 状态保存 |
| 每 1200 | inductive_apply | 归纳规则应用 |
| 每 2000 | TLN 进化 | 0.5% 权重微变异 |
| 每 5000 | full persist | 完整保存 + TLN 持久化 |

### TLN 与调度器的交互

- 代码修改前: TLN `modify_hint = -1` 则否决
- drive 调制: TLN `action_hint` 修正 drive 方向
- 每 tick: TLN 输出写入 Soul 供 torkd 读取

---

## 9. 沙箱系统

### 命令沙箱 (`src/sandbox/sandbox.h`)

命令分类 (READ/WRITE/EXEC/NET/SYS/DANGEROUS)，根据协议授权的沙箱等级决定是否允许执行。`sandbox_exec()` 受约束执行，超时控制，固定缓冲区输出。

### 沙箱启动器 (`sandbox/tork_sandbox.c`)

Linux namespace 隔离启动器，执行完整沙箱化：

1. **clone()** 创建子进程，flags: `CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWIPC` (非 root 加 `CLONE_NEWUSER`)
2. **pivot_root()** 切换根文件系统到 `/tmp/tork_rootfs`
3. **bind-mount**: build/ (只读), persist/ (读写), /sys/thermal (只读), /etc/tork (只读), /lib64 + /lib (只读)
4. **mount**: /proc (nosuid), /tmp (tmpfs 64m), /dev (tmpfs 4m), /dev/shm (tmpfs 32m)
5. **cgroup v2**: pids.max=64, memory.max=512MB, cpu.max=50%

子进程在隔离环境中 `execv("/build/tork_engine", "--daemon")`。

### 协议与授权 (`src/install/agreement.h`)

EULA 系统，五种沙箱等级：NONE(0) / READ(1) / SAFE(2) / NORMAL(3) / FULL(4)。协议签名存储在 `/etc/tork/agreement.sig`，含 CRC32 校验。C 引擎启动时读取签名，写入 S_AGREED 和 S_SANDBOX_LEVEL。

---

## 10. 网络服务

### torkd (Unix Socket 服务)

嵌入式 socket 服务，直接嵌入主循环，每 tick 非阻塞处理客户端。外部程序通过 `/tmp/torkd.sock` 查询 TORK 状态。

### Web 仪表盘 (`web/tork_web.py`)

aiohttp + WebSocket 架构：

- `/` — 仪表盘 HTML (CodeMirror 6 编辑器)
- `/ws` — WebSocket 实时推送 (Soul 状态 + 本能 + 进化统计)
- `/api/exec` — 命令执行 (经 torkd)
- `/api/evolve` — 触发云端进化
- `/api/file/{path}` — 文件读写 (路径安全检查)
- `/api/config` — API 配置 (模型/密钥)
- `/api/dir/{path}` — 目录浏览

后台 poll_loop 每秒通过 `read_soul_from_proc()` 或 `torkd_query("soul")` 获取 Soul 状态，推送到所有 WebSocket 客户端。

### torkd 桥接层 (`web/torkd_bridge.py`)

异步桥接：`torkd_query()` 通过 ThreadPoolExecutor + Semaphore(4) 并发查询 Unix Socket，超时 10s。

### API 层 (`api/tork_api.py`)

讯飞星辰 MaaS 接口，默认模型 `astron-code-latest`。支持有状态对话 (`ask()`) 和无状态单次请求 (`ask_simple()`)。TORK 的云端导师——指导代码进化、分析运行状态、提供技术指导。

### 云端进化 (`cloud/evolution.py`)

混合架构：规则引擎 (安全可编译变异) + LLM (战略指导) + 适应度反馈。10 种变异策略轮转，可被 fitness best_mutagen 覆盖。变异 → 备份 → 编译测试 → 成功则 git commit → 世代递增。支持持续进化模式 (`--loop`)。

---

## 11. 持久化与恢复

### Persistor (`src/engine/persistor.h`)

- `ps_save_all()`: 保存 blackboard + params + rules + Soul 到 `/tmp/tork/persist/`
- `ps_restore_all()`: 从磁盘恢复所有共享内存区域
- `ps_restore_soul()`: 恢复 Soul 数据
- `ps_decay_memory()`: 衰减低价值规则，修剪过期数据
- `ps_hot_swap()`: 保存状态 → fork+exec 新二进制 → 父进程退出
- `ps_emergency_save()`: 信号安全保存（从注册的 Soul 缓冲区写入，避免读 unmapped 0x200000）

### 恢复流程

`--restore` 启动时：`ps_restore_all()` 恢复共享内存 → `ps_restore_soul()` 恢复 Soul tick → 写入 L1 进程 → 从恢复的 tick 继续。

---

## 12. 直觉与本能

### Instinct (本能系统)

三力模型：**fear** (恐惧) / **desire** (欲望) / **curiosity** (好奇心)

```
drive = (desire - fear + curiosity) * 100  →  [-128, +127]
```

输入 22 个信号：tick, elapsed, hw_stress, mode, code_insns, code_ctrl, code_mod_success, code_opt_saved, fission_count, wins, bb_global_opts, active_rules, rule_applied, restored_files, save_success, idle_discoveries, branch_active_count, pattern_best_action, pattern_confidence, energy_mode, energy_throttle, env_changed。

**哲学指向**: 三力不是权重参数——是生存的三个维度。恐惧让它保守，欲望让它进取，好奇心让它探索未知。三者之和就是 drive，写回 Soul 驱动所有决策。

### Monitor (硬件监控)

`monitor_parse_proc_status()`: 读取 `/proc/PID/status` 中的指定字段（如 PPid）。轻量级，只做一件事。

---

## 13. 构建与部署

### Makefile 目标

| 目标 | 说明 |
|------|------|
| `all` | 编译全部: tork_core, tork_engine, tork_sandbox, tork_sandbox_launcher, tork_ask, torkd_start, tork, tork_grid |
| `run` | 运行 10 轮 |
| `run100` | 运行 100 轮 |
| `sandbox` | 编译沙箱启动器 |
| `start` | 守护进程启动 |
| `stop` | 停止 |
| `test` | 单元测试 |
| `appimage` | 构建 AppImage 安装包 |
| `install` | 安装到系统 |

### 编译工具链

- ASM: `as` (GNU Assembler) → `ld` (Linker)
- C: `gcc -Wall -Wextra -O2`
- Python: 3.x (aiohttp, requests)
- 依赖: `-lm` (数学), `-lrt` (实时扩展)

### AppImage

双击启动 Web UI。`scripts/build-installer.sh` 构建 AppImage，内含全部二进制 + Python 仪表盘。启动时自动打开浏览器到 `http://localhost:8420`。

---

## 14. 版本演进史

### v1.0 — 创世纪

纯 ASM 心跳核心。TOR 运算、Soul 基础字段 (0x00-0x46)、温度感知、僵死检测。192 字节 Soul 的第一版。TORK 会跳了。

### v2.0 — 觉醒

Soul 扩展：S_AGREED, S_SANDBOX_LEVEL, S_CLOUD_CONNECTED, S_LEARN_COUNT, S_MUTATION_COUNT, S_BEST_SCORE, S_GEN_COUNT。协议系统、沙箱分级、云端连接。心跳间隔可改写。TORK 开始知道自己在哪。

### v3.0 — 学习

经验环缓冲区 (4096 条)、模式学习 (64 槽)、MCTS、本能三力模型、自调参、观察者、快照/回滚、能耗管理。Soul 学习字段 (0x60-0x75)。TORK 开始从自身经验中学习。

### v3.1 — 分岔

灵魂分岔引擎：主干分出短暂生命的变体，死亡回报经验。Soul 分支字段 (0x80-0xA6)。归纳学习 (inductor)、事件观察器 (watcher)。TORK 开始并行探索。

### v3.14 — MCTS 管道

MCTS 代码修改管道、架构弱点修正、单元测试。代码变异从简单替换升级为树搜索。

### v3.15 — TLN 推理

三进制逻辑网络 (16→32→8)、TLN hint 字段 (0x76-0x79)、pi-seed 随机源、pi 索引记忆层、变异引导引擎。安全审计修复。TORK 有了"第三个选项"——不是确定也不是否定，是悬置。

### v3.16 — 变频心跳

讯飞星辰 MaaS 集成 (astron-code-latest)、双击 AppImage 启动 Web UI、aiohttp 仪表盘 + CodeMirror 6 编辑器。pi-heartbeat 变频机制成熟：self_tune 根据学习信心动态调整心跳间隔，通过 Soul 写入控制 L1 节奏。TORK 开始自主调节自己的脉搏。

---

## 附录 A: 关键内存地址

| 地址 | 大小 | 内容 |
|------|------|------|
| 0x200000 | 192B | Soul 共享内存 (L1/L2 通信) |
| 0x300000 | 4KB | Blackboard 共享事件区 |

## 附录 B: 持久化文件

| 路径 | 内容 |
|------|------|
| persist/experience.bin | 经验环缓冲区 |
| persist/patterns.bin | 模式学习库 |
| persist/tune_params.bin | 自调参参数 |
| persist/tln.bin | TLN 网络权重 |
| persist/snapshot.bin | 快照历史 |
| persist/self_build.bin | 自编译状态 |
| persist/pi_index.bin | pi 索引 |
| persist/mutation_guide.bin | 变异引导策略 |
| persist/evolution.json | 云端进化日志 |
| /tmp/tork/persist/ | Persistor 全局保存目录 |
| /tmp/tork_seed_N.bin | Colony 种子文件 |
| /etc/tork/agreement.sig | 协议签名 |

## 附录 C: 网络端点

| 端点 | 协议 | 说明 |
|------|------|------|
| /tmp/torkd.sock | Unix Socket | torkd 查询 |
| 127.0.0.1:8420 | HTTP/WS | Web 仪表盘 |
| 239.42.69.42:42069 | UDP 多播 | 分布式黑板 |
| /dev/shm/tork_soul.bin | 共享内存 | Grid 连接器 |

---

*本文档由 TORK v3.16 代码库自动生成。所有偏移、字段、结构体均与 `tork_soul.inc` 和 `soul_access.h` 严格对齐。*
