# LEARNING.md — 学习闭环

> 5 秒摘要：TORK 的学习由 5 个子系统组成：经验环（记录每次行动的结果）→ 模式学习（从经验提取统计规律）→ MCTS 搜索（在行动空间中做树搜索）→ 归纳推理（从经验生成规则）→ TLN 三值网络（实时推理修正 drive）。快照自愈提供安全网。

---

## 1. 经验环 (Experience Ring)

### 1.1 功能述求

系统需要记住"做了什么、结果如何"，这是所有学习的基础。经验环是一个固定大小的环形缓冲区，自动覆盖最旧记录。

### 1.2 数据结构

`learning/experience.h`

```
experience_t:
  tick            uint64    哪个 tick 发生的
  timestamp_ns    int64     纳秒时间戳
  hw_stress       uint8     行动前的压力
  drive_pre       int8      行动前的 drive
  gen_count       uint16    当时的世代数
  action_type     uint8     行动类型 (0-10)
  action_param    int8      行动参数
  outcome         int8      结果评分 (-128 ~ +127)
  crash_occurred  uint8     是否崩溃
  compile_ok      uint8     是否编译通过
  hw_stress_post  uint8     行动后的压力
  drive_post      int8      行动后的 drive
```

环形缓冲区：4096 条经验，`head` 指针循环推进，`count` 记录总数。

### 1.3 写入

```
exp_record(tick, hw_stress, drive_pre, gen_count,
           action_type, action_param, outcome, crash, compile_ok,
           hw_stress_post, drive_post)
```

- 写入 `slots[head]`
- `head = (head + 1) % 4096`
- 每 10 条自动保存到 `persist/experience.bin`

### 1.4 查询

| 函数 | 作用 |
|------|------|
| `exp_recent(n, out)` | 最近 n 条经验 |
| `exp_filter(action_type, max, out)` | 按行动类型过滤 |
| `exp_success_rate(action_type)` | 某行动类型的成功率（最近 1000 条） |
| `exp_update_last(outcome, ...)` | 回填最近一条经验的 outcome |

### 1.5 关键路径

所有行动都通过 `tork_dispatch()` → `exp_record()` 写入经验。没有其他写入路径。

---

## 2. 模式学习 (Pattern Learner)

### 2.1 功能述求

经验是原始数据，模式是从经验中提取的统计规律。模式回答："在压力=X、drive=Y、世代=Z 的条件下，行动 A 的平均结果是什么？"

### 2.2 量化键

模式不直接用连续值做键，而是量化为离散桶：

```
hw_stress:  0-3 (直接使用)
drive:      量化为 8 档 (-4..3)
  -80 以下 → -4
  -50 以下 → -3
  -20 以下 → -2
  0 以下   → -1
  20 以下  → 0
  50 以下  → 1
  80 以下  → 2
  其他     → 3

gen_count:  量化为 4 桶
  < 5   → 0
  < 20  → 1
  < 100 → 2
  其他  → 3
```

模式键 = (hw_stress, drive_bucket, gen_bucket, action_type)

### 2.3 学习流程

`learning/pattern.c:93-134` (`pat_learn_from_experience`)

```
1. 读取最近 200 条经验
2. 对每条经验:
   a. 跳过 outcome=0 且无崩溃的（未回填的）
   b. 构建模式键
   c. 查找或创建模式槽位 (最多 64 个)
   d. 更新统计: total_outcome, total_crashes, sample_count
   e. 计算 avg_outcome = total_outcome / sample_count
   f. 计算 crash_rate = total_crashes / sample_count
```

### 2.4 查询最佳行动

`learning/pattern.c:137-188` (`pat_query_best_action`)

```
对每个匹配当前 (hw_stress, drive_bucket, gen_bucket) 的模式:
  1. 时效衰减: decayed_outcome = pi_decay(avg_outcome, age, half_life=100)
     age = 当前 learn_cycle - last_seen_tick
  2. 综合评分: score = decayed_outcome * sqrt(sample_count)
  3. 惩罚崩溃: score *= (1.0 - crash_rate * 0.5)
  4. 取 score 最高的行动
```

时效衰减确保旧模式不会永远主导。`pi_decay` 使用半衰期 100 个 learn_cycle。

### 2.5 槽位管理

- 最多 64 个模式槽位
- 满时覆盖最久未使用的（LRU）
- 持久化到 `persist/patterns.bin`

---

## 3. MCTS 搜索 (Monte Carlo Tree Search)

### 3.1 功能述求

当系统处于闲时，MCTS 在 11 种行动的空间中搜索最优行动。它不是在棋盘上搜索，而是在"压力/drive/世代"的状态空间中搜索。

### 3.2 行动空间

`learning/mcts.h`

| 编号 | 行动 | 含义 |
|------|------|------|
| 0 | MCTS_ADJUST_FEAR | 调整恐惧权重 |
| 1 | MCTS_ADJUST_CURIOSITY | 调整好奇权重 |
| 2 | MCTS_ADJUST_HEARTBEAT | 调整心跳间隔 |
| 3 | MCTS_TRY_MODIFY | 尝试代码修改 |
| 4 | MCTS_TRY_OPTIMIZE | 尝试代码优化 |
| 5 | MCTS_ENTER_IDLE | 进入闲时 |
| 6 | MCTS_CALL_CLOUD | 调用云端 |
| 7 | MCTS_MOD_REPLACE_OP | 替换操作数 |
| 8 | MCTS_MOD_DEL_DEAD | 删除死代码 |
| 9 | MCTS_MOD_DEL_NOP | 删除 NOP |
| 10 | MCTS_MOD_SWAP_REGS | 交换寄存器 |

### 3.3 搜索流程

`learning/mcts.c:231-296` (`mcts_search`)

```
1. 创建根节点，从历史经验填充 exp_success[]
2. 计算迭代次数: iterations = time_budget_ms * 100 (上限 50000)
3. 每次迭代:
   a. SELECT: 从根沿 UCB1 最高路径走到叶节点
   b. EXPAND: 如果叶节点已访问过，展开所有 11 个子节点
   c. SIMULATE: 启发式评估叶节点价值
   d. BACKPROPAGATE: 沿路径更新 visit_count 和 total_value
4. 选择平均价值最高的子节点
```

### 3.4 UCB1 公式

```
UCB1 = exploitation + exploration
     = (total_value / visit_count) + C * sqrt(ln(parent_visits) / visit_count)
```

C 是探索常数，由 `mcts_auto_tune()` 动态调整。

### 3.5 模拟（Rollout）

`learning/mcts.c:142-198` (`simulate_node`)

**注意：这不是真实模拟，是启发式评分。**

```
value = 0
value += (3.0 - hw_stress) * 0.1    // 低压力好
value += drive * 0.003               // 正 drive 好
value += gen_count * 0.001           // 多世代好

// 代码修改子动作的特殊评分
replace_op:  低压力时 +0.25, 否则 -0.1
del_dead:    总是 +0.2
del_nop:     总是 +0.15
swap_regs:   低压力时 +0.2, 否则 -0.05

// 历史成功率
value += exp_success[action_type] * 0.3

// 随机探索
value += pi_seed_float() * 0.1
```

### 3.6 自动调参

`learning/mcts.c:398-438` (`mcts_auto_tune`)

每 10 个闲时周期调用一次：

```
if 平均 outcome < -10:  exploration += 0.1  (结果差，多探索)
if 平均 outcome > +15:  exploration -= 0.05 (结果好，多利用)

if 崩溃率 > 20%:       min_iterations += 50  (不稳定，多搜索)
if 崩溃率 < 5%:        min_iterations -= 50  (稳定，少搜索)
```

---

## 4. 归纳推理 (Inductor)

### 4.1 功能述求

从黑板的成功/失败记录中提取规则，如"如果函数包含 je 指令 → 替换为 jz"。规则有置信度，经过测试后可以激活。

### 4.2 规则结构

`engine/inductor.h`

```
tork_rule:
  type            uint8     规则类型 (1=代码)
  active          uint8     是否激活
  confidence      uint8     置信度 (0-100%)
  premise[32]     char      前提描述
  conclusion[32]  char      结论描述
  apply_count     uint16    成功应用次数
  fail_count      uint16    失败次数
```

存储在固定地址共享内存 `0x302000`，最多 32 条规则。

### 4.3 提取流程

`engine/inductor.c:142-203` (`ind_extract_experiences`)

从黑板读取 3 类成功/失败记录，生成 3 条候选规则：

| 类型 | 前提 | 结论 |
|------|------|------|
| 1 | "function contains 'je' instr" | "replace 'je' with 'jz'" |
| 2 | "unreachable code after 'ret'" | "delete unreachable code" |
| 3 | "function contains nop/align" | "delete nop alignment" |

置信度 = 成功次数 / (成功 + 失败) * 100

### 4.4 泛化

`engine/inductor.c:206-253` (`ind_generalize`)

合并多条同类规则：取最低置信度、累加应用/失败次数、取最早 tick。

### 4.5 测试与激活

`engine/inductor.c:256-328` (`ind_test_rule`)

1. 读取目标 ASM 文件
2. 检查前提是否匹配（字符串搜索）
3. 执行结论（实际修改代码 + 编译验证）
4. 更新置信度
5. 置信度 >= RULE_CONFIDENCE_ACTIVE → 激活
6. 置信度 <= RULE_CONFIDENCE_RETIRE → 退役

### 4.6 生命周期

```
提取 (每 800 tick) → 测试 (每 1000 tick) → 激活 → 应用 (每 1200 tick)
                                                      ↓
                                              置信度衰减 (每 5000/10000 tick)
                                                      ↓
                                              退役或恢复
```

---

## 5. TLN 三值逻辑网络 (Ternary Logic Network)

### 5.1 功能述求

TLN 是一个实时推理网络，每 tick 做一步前向推理，输出 4 个决策维度来修正 drive 和否决代码修改。权重空间 {-1, 0, +1}，纯整数运算，无浮点无乘法器。

### 5.2 网络结构

`engine/tln.h`

```
输入:  16 个三值神经元 (TLN_INPUTS)
隐藏:  8 个三值神经元 (TLN_HIDDEN)
输出:  8 个三值神经元 (TLN_OUTPUTS)

权重矩阵:
  w_ih[8][16]    输入→隐藏
  w_hh[8][8]     隐藏→隐藏 (自环，时序推理的核心)
  w_ho[8][8]     隐藏→输出

每个权重 ∈ {-1, 0, +1}
```

### 5.3 前向推理

`engine/tln.c:27-56` (`tln_step`)

```
// 隐藏层
for j in 0..7:
    sum = 0
    sum += Σ w_ih[j][i] * input[i]     // 外部输入
    sum += Σ w_hh[j][k] * state[k]     // 上一 tick 的隐藏状态回接
    hidden[j] = clamp(sum, -1, 0, +1)  // 三值钳位

// 输出层
for j in 0..7:
    sum = Σ w_ho[j][k] * hidden[k]
    output[j] = clamp(sum, -1, 0, +1)

// 更新状态
state = hidden
```

关键：`w_hh` 自环使网络具有时序记忆——上一 tick 的隐藏状态影响当前 tick，形成递归逻辑。

### 5.4 输入编码

`engine/tln.c:62-102` (`tln_encode_soul`)

16 个输入从 Soul 字段离散化而来：

| 输入 | 来源 | 编码 |
|------|------|------|
| 0-2 | hw_stress | 0→+1, 1→-1, 2+→-1, 3→-1 |
| 3-5 | drive | >30→+1, <-30→-1, >60→+1, <-60→-1 |
| 6-7 | gen_count | >6→+1, >10→+1 |
| 8-11 | pattern_out | 模式推荐/置信度/冲突/强度 |
| 12 | S_MODE | >=2→-1, ==1→+1, else→0 |
| 13 | S_CODE_MOD_SUCCESS | ==1→+1, else→-1 |
| 14 | S_CODE_OPT_SAVED | >0→+1, else→0 |
| 15 | S_FISSION_COUNT | >0→+1, else→0 |

### 5.5 输出解码

`engine/tln.c:109-129` (`tln_decode_output`)

8 个输出两两配对，解码为 4 个决策维度：

| 维度 | 输出对 | 含义 |
|------|--------|------|
| action_hint | output[0] + output[4] | +1=激进, -1=保守, 0=悬置 |
| modify_hint | output[1] + output[5] | +1=可变异, -1=禁变异, 0=悬置 |
| explore_hint | output[2] + output[6] | +1=探索, -1=收敛, 0=悬置 |
| energy_hint | output[3] + output[7] | +1=高功率, -1=省电, 0=悬置 |

双输出设计：两个输出的和可以表达更强的确定性（+2 或 -2）或更弱的悬置（+1 和 -1 抵消为 0）。

### 5.6 变异

`engine/tln.c:148-164` (`tln_mutate`)

每 2000 tick，以 0.5% 概率独立变异每个权重：

```
+1 → 0 或 -1 (减弱或反转)
 0 → +1 或 -1 (增强或反转)
-1 → 0 或 +1 (减弱或反转)
```

新值由 `pi_seed_from_tsc()` 决定（基于 TSC 的确定性随机）。

**注意：TLN 没有梯度学习算法。** 权重只通过随机变异改变，选择压力来自编译验证（变异后如果系统行为恶化，快照自愈会回滚）。这是一个已知的局限性。

### 5.7 持久化

`engine/tln.c:189-224`

保存到 `persist/tln.bin`，格式：4 字节 magic (0x544C4E00 = "TLN\0") + sizeof(TernaryNet) 字节数据。使用原子写入（先写 .tmp → rename）。

---

## 6. 快照自愈 (Snapshot)

### 6.1 功能述求

系统可能退化（drive 持续下降、压力飙升、Soul CRC 损坏）。快照引擎定期保存状态，检测退化时自动回滚到最佳快照。

### 6.2 快照结构

```
snapshot_t:
  tick        uint64
  drive       int64
  hw_stress   uint8
  gen_count   uint64
  soul_data   uint8[208]    // 完整 Soul 副本
  checksum    uint32        // CRC32
```

环形历史：最多 SNAP_MAX_HISTORY 个快照。

### 6.3 自动快照

`snap_auto()`: 每 SNAP_AUTO_INTERVAL tick 自动保存一个快照。

`snap_commit()`: 在 drive 稳定且健康时，以指数退避间隔（50 → 100 → 200 → ... → 800 tick）提交"确认健康"的快照。

### 6.4 健康检查

`learning/snapshot.c:71-128` (`snap_health_check`)

退化条件：

| 条件 | 检测 |
|------|------|
| drive 大幅下降 | drive < last.drive - 30 && drive < -20 |
| drive 持续恶化 | drive < 0 && last.drive < 0 && drive < last.drive - 10 |
| 压力飙升 | hw_stress > last.hw_stress + 1 && hw_stress >= 3 |
| Soul CRC 失败 | soul_crc_ok == 0 |

### 6.5 回滚

`snap_rollback()`: 找到 drive 最高的快照，复制其 soul_data，通过 `soul_write_buf()` 写回 tork_core 的内存。

---

## 7. 闭环总览

```
                    ┌─────────────────────────────────────────┐
                    │           tork_dispatch()                │
                    │  所有行动的唯一入口                       │
                    └──────────────┬──────────────────────────┘
                                   │
                    ┌──────────────▼──────────────────────────┐
                    │         exp_record()                     │
                    │  写入经验环 (4096 条)                    │
                    └──────┬───────────┬──────────────────────┘
                           │           │
              ┌────────────▼┐     ┌────▼─────────┐
              │ pat_learn    │     │ ind_extract   │
              │ 模式学习     │     │ 归纳提取      │
              │ (每 20 tick) │     │ (每 800 tick) │
              └──────┬──────┘     └──────┬────────┘
                     │                   │
              ┌──────▼──────┐     ┌──────▼─────────┐
              │ pat_query   │     │ ind_test/apply  │
              │ 模式查询     │     │ 归纳测试/应用   │
              │ (每 tick)   │     │ (每 1000/1200) │
              └──────┬──────┘     └────────────────┘
                     │
              ┌──────▼──────────────────────────────┐
              │ instinct_evaluate() → drive           │
              │ 三驱力计算 (模式影响 fear/desire/cur) │
              └──────┬──────────────────────────────┘
                     │
              ┌──────▼──────────────────────────────┐
              │ TLN 推理 → 修正 drive ± 8            │
              │ (每 tick)                            │
              └──────┬──────────────────────────────┘
                     │
              ┌──────▼──────────────────────────────┐
              │ 写回 Soul S_DRIVE                    │
              │ 下一 tick 的心跳受 drive 影响         │
              └──────────────────────────────────────┘

    闲时路径:
    tick_idle → idler_cycle → mcts_search → idler_mcts_modify → tork_dispatch → exp_record

    安全网:
    tick_snapshot → snap_health_check → snap_rollback → soul_write_buf
```

---

## 验证清单

- [ ] 运行引擎后 `ls -la persist/experience.bin persist/patterns.bin persist/tln.bin` 确认学习数据已持久化
- [ ] 观察输出中 "PAT: learned X new patterns" 确认模式学习在工作
- [ ] 观察输出中 "MCTS: action=..." 确认闲时 MCTS 搜索在工作
- [ ] 观察输出中 "IND: generalized rule" 确认归纳推理在工作
- [ ] 观察输出中 "TLN: evolved X weights" 确认 TLN 变异在工作
- [ ] 观察输出中 "SNAP: ROLLBACK" 确认快照自愈在工作（需要制造退化条件）
- [ ] `python3 -c "import struct; d=open('persist/tln.bin','rb').read(); print('magic:', hex(struct.unpack_from('I',d,0)[0]))"` 确认 TLN 文件格式正确