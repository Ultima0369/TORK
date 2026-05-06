# SCHEDULER.md — 调度器与周期任务

> 5 秒摘要：`scheduler_tick()` 是所有周期任务的唯一入口，每 500ms 调用一次。它按固定间隔调度 20+ 个任务：代码读取/修改/优化、模式学习、归纳推理、持久化、TLN 推理/进化、快照自愈、闲时 MCTS 搜索、分支管理、π-节奏等。

---

## 1. 功能述求

TORK 有大量需要周期执行的任务，频率从每 tick 到每 10000 tick 不等。调度器统一管理这些任务的触发时机和执行顺序，避免散落的定时器逻辑。

---

## 2. scheduler_tick 完整时序

`engine/scheduler.c:754-904`

每 tick 的执行顺序（按代码出现顺序）：

```
1.  停滞检测                    每 tick
2.  bb_set_tick()               每 tick
3.  tick_pattern_learn()        内部每 20 tick
4.  tick_services()             每 tick
    ├─ grid_soul_feed()         每 tick
    ├─ torkd_tick()             每 tick
    ├─ task_process_one()       每 tick
    ├─ dist_tick()              每 tick
    └─ TLN 推理                 每 tick
5.  tick_code_read()            每 200 tick
6.  tick_code_modify()          每 10 tick (TLN 可否决)
7.  tick_code_optimize()        每 30 tick
8.  tick_nop_delete()           每 50 tick
9.  tick_inductive_apply()      每 1200 tick
10. tick_fission()              每 1000 tick
11. tick_inductive()            每 800 tick
12. tick_inductive_test()       每 1000 tick
13. tick_persist()              1000/5000/10000 tick
14. 重新计算 instinct + drive   每 tick
15. TLN 修正 drive              每 tick
16. 模式查询                    每 tick
17. tick_branch()               每 tick
18. tick_pi_rhythm()            每 tick
19. tick_observer_energy()      每 tick
20. tick_monitoring()           10/50/100/200 tick
21. tick_snapshot()             每 tick
22. tick_feedback()             每 tick (延迟 50 tick 评估)
23. tick_idle()                 每 100 tick
24. TLN 进化 (0.5% 变异)       每 2000 tick
25. tick_print()                每 10 tick
```

---

## 3. 各任务详解

### 3.1 tick_services（每 tick）

`engine/scheduler.c:57-115`

- **grid_soul_feed()**: 把 Soul 数据推送到 `/dev/shm/tork_soul.bin`，供网格节点读取
- **torkd_tick()**: 处理 Unix Socket 上的待处理连接（非阻塞 accept）
- **task_process_one()**: 处理异步任务队列中的一个任务
- **dist_tick()**: 分布式节点心跳
- **TLN 推理**:
  1. `tln_encode_soul()` — 把 Soul 字段编码为 16 个三值输入 {-1, 0, +1}
  2. `tln_step()` — 前向推理一步（整数加法 + 钳位）
  3. `tln_decode_output()` — 8 个输出解码为 4 个决策维度
  4. `soul_write_buf(S_TLN_ACTION, ...)` — 写回 Soul

### 3.2 tick_code_read（每 200 tick）

`engine/scheduler.c:132-162`

1. 读取 `benchmark/memcpy/ref.s`
2. 统计 `memcpy_tork` 函数的指令数和分类 (mov/arith/ctrl/other)
3. 提取前 10 个操作码
4. 写入 Soul 的 S_CODE_INSNS ~ S_CODE_OTHER 字段

### 3.3 tick_code_modify（每 10 tick）

`engine/scheduler.c:165-208`

1. 读取 `benchmark/memcpy/ref.s`
2. 备份原始内容
3. 尝试替换 `\tje\t` → `\tjz\t`（语义等价指令替换）
4. `asm_verify_modification()` — 编译 + 运行 benchmark 验证正确性
5. 通过 → 写回文件，更新 Soul S_CODE_MOD_SUCCESS = 1
6. 失败 → `asm_rollback()` 恢复备份，S_CODE_MOD_SUCCESS = 2

**TLN 否决**：如果 `tln_modify_hint == -1`，跳过此任务。

### 3.4 tick_code_optimize（每 30 tick）

`engine/scheduler.c:211-249`

1. 尝试 `asm_delete_dead_insns()` — 删除 ret 后的不可达代码
2. 如果无死代码，尝试 `asm_delete_nop_insns()` — 删除 NOP 对齐指令
3. 编译验证 → 通过则写回，失败则回滚

### 3.5 tick_nop_delete（每 50 tick）

`engine/scheduler.c:252-294`

专门删除 NOP 指令，与 tick_code_optimize 分开是因为 NOP 删除更安全，可以更频繁执行。

### 3.6 tick_pattern_learn（内部每 20 tick）

`engine/scheduler.c:118-129`

```
pat_learn_from_experience()   → 从经验缓冲区提取模式
tune_adjust_from_patterns()   → 根据模式调整自调参权重
instinct_apply_tune()         → 更新本能权重
每 40 tick: pat_save()        → 持久化模式库
```

### 3.7 tick_inductive（每 800 tick）

`engine/scheduler.c:350-369`

1. `ind_extract_experiences()` — 从黑板提取经验，生成归纳规则
2. `ind_generalize()` — 合并同类规则，提升置信度
3. `ind_save_rule()` — 保存到共享内存

### 3.8 tick_inductive_test（每 1000 tick）

`engine/scheduler.c:372-386`

1. `ind_find_pending()` — 找到置信度 > 60% 但未激活的规则
2. `ind_test_rule()` — 在实际代码上测试规则
3. `ind_update_rule()` — 更新置信度，如果 >= 阈值则激活

### 3.9 tick_inductive_apply（每 1200 tick）

`engine/scheduler.c:389-410`

1. `ind_find_active()` — 找到已激活的规则
2. `ind_test_rule()` — 应用规则到代码
3. 成功 → `inp->rule_applied = 1`

### 3.10 tick_fission（每 1000 tick）

`engine/scheduler.c:297-347`

1. `fission_decide()` — 检查分裂条件（tick > 5000, stress < 2, drive > 20, π 随机 60% 概率通过）
2. `tork_dispatch(DISP_SELF_FISSION)` — 通过 dispatch 闭环记录
3. `fission_spawn()` — fork 子进程，复制项目结构，运行 500 轮
4. `fission_migrate()` — 竞争：子进程更好则父进程退出，否则杀子进程

### 3.11 tick_persist

`engine/scheduler.c:413-441`

| 间隔 | 动作 |
|------|------|
| 1000 tick | `ps_save_all()` — 保存 Soul + 黑板 + 参数 + 规则 |
| 5000 tick | 完整保存 + `ps_decay_memory()` — 规则衰减 |
| 10000 tick | `ps_decay_memory()` — 再次衰减 |
| 5000 tick | `tln_save()` — 保存 TLN 权重 |

### 3.12 tick_observer_energy（每 tick）

`engine/scheduler.c:474-496`

- `obs_sample()` — 采样当前状态（每 OBS_SAMPLE_INTERVAL tick）
- `obs_check_anomaly()` — 检测异常（drive/stress 偏离基线）
- `self_cal_tick()` — 自校准心跳倍率
- `eng_update()` — 更新能量状态
- `eng_should_limit_branches()` — 高负载时限制分支

### 3.13 tick_monitoring

`engine/scheduler.c:444-471`

| 间隔 | 动作 |
|------|------|
| 10 tick | `watcher_scan_proc()` — 扫描 /proc 下的进程 |
| 100 tick | `watcher_learn_patterns()` — 学习进程模式 |
| 50 tick | `sb_check_sources()` — 检测源文件变更 |
| 200 tick | `mg_recommend()` — 变异引导推荐 |

### 3.14 tick_snapshot（每 tick）

`engine/scheduler.c:499-536`

1. `snap_auto()` — 自动快照（每 SNAP_AUTO_INTERVAL tick）
2. `snap_health_check()` — 健康检查：
   - drive 从正值跌到 < -20 → 退化
   - drive 持续为负且越来越差 → 退化
   - stress 飙升 → 退化
   - Soul CRC 失败 → 退化
3. 退化 → `snap_rollback()` — 回滚到 drive 最高的快照，通过 ptrace 写回 Soul

### 3.15 tick_idle（每 100 tick）

`engine/scheduler.c:636-694`

1. `idler_should_enter()` — 检查闲时条件：
   - hw_stress < 3
   - rounds_since_mod >= 300（最近 300 tick 无修改）
   - drive >= -10（不太恐惧）
   - 距上次黑板活动 > 200 tick
2. 进入闲时 → `idler_cycle()`:
   - 构建 MCTS 状态
   - `mcts_search()` — 500ms 预算搜索
   - 记录经验
   - 每 10 轮 `mcts_auto_tune()`
   - 每 3 轮 `pat_learn_from_experience()`
   - 每 5 轮 `replay_deep()` — 深度重放
   - 每 2 轮 `obs_update_baseline()` — 更新基线
3. 如果 MCTS 推荐代码修改 → `idler_mcts_modify()` → 通过 dispatch 闭环执行
4. 退出闲时 → 设置 feedback_pending，50 tick 后评估结果

### 3.16 tick_feedback

`engine/scheduler.c:697-725`

闲时退出后 50 tick 评估效果：

```
outcome = 0
if hw_stress 下降:     outcome += 20
if hw_stress 上升:     outcome -= 15
if drive 上升 > 10:    outcome += 30
if drive 上升 > 0:     outcome += 10
if drive 下降 < -10:   outcome -= 20
if drive 下降 < 0:     outcome -= 5
if 黑板优化增加:       outcome += 25

exp_update_last(outcome, ...)
```

### 3.17 TLN 进化（每 2000 tick）

`engine/scheduler.c:894-898`

```
tln_mutate(&tln, 0.005f)  → 以 0.5% 概率随机变异每个权重
```

变异规则：三值空间中的随机跳转（+1 → 0/-1, 0 → +1/-1, -1 → 0/+1）。

---

## 4. dispatch 闭环

### 4.1 功能述求

所有"行动"必须经过 `tork_dispatch()`，这样每次行动的结果都会自动写入经验缓冲区。这是学习闭环的关键——没有遗漏的行动。

### 4.2 流程

`engine/dispatch.c:65-441`

```
tork_dispatch(input)
  │
  ├─ 执行 action (10 种)
  │   ├─ DISP_EXEC_CMD       → sandbox_exec()
  │   ├─ DISP_ANALYZE_ASM    → asm_read_file + asm_classify_insns
  │   ├─ DISP_AUDIT_CODE     → audit_asm_file
  │   ├─ DISP_SELF_MODIFY    → asm_replace_operand + verify
  │   ├─ DISP_SELF_OPTIMIZE  → asm_delete_dead_insns/nop + verify
  │   ├─ DISP_SELF_MCTS_MOD  → MCTS 修改 (4 种子动作)
  │   ├─ DISP_CODEGEN_SEARCH → codegen_mcts_search
  │   ├─ DISP_CODEGEN_COMPILE → codegen_compile_verify
  │   ├─ DISP_CODEGEN_BENCH  → codegen_benchmark
  │   └─ DISP_SELF_FISSION   → fission_spawn
  │
  ├─ compute_outcome()        → 从结果推导经验值
  │   ├─ EXEC_CMD 成功:  +20, 失败: -15
  │   ├─ AUDIT 有输出:   +15, 无输出: -10
  │   ├─ SELF_MODIFY 通过: +60, 失败: -20
  │   ├─ CODEGEN_SEARCH 有分: +40, 无分: -20
  │   ├─ CODEGEN_COMPILE 通过: +30, 失败: -15
  │   ├─ CODEGEN_BENCH 有值: +25, 无值: -10
  │   └─ FISSION 成功: +10, 失败: -20
  │
  ├─ exp_record()             → 写入经验缓冲区
  │
  └─ 每 10 次成功: pat_learn_from_experience()  → 触发模式学习
```

---

## 5. 周期任务速查表

| 间隔 | 任务 | 条件 |
|------|------|------|
| 1 | TLN 推理 + 写回 Soul | tln_enabled |
| 1 | grid/torkd/task/dist 服务 | — |
| 1 | instinct 重算 + drive 合成 | — |
| 1 | TLN drive 修正 | tln_enabled |
| 1 | 模式查询 | — |
| 1 | 分支推进 | — |
| 1 | π-节奏 | — |
| 1 | 观察者 + 能量 | — |
| 1 | 快照 + 健康检查 | — |
| 1 | 反馈评估 | feedback_pending && 延迟 50 tick |
| 10 | 代码修改 (je→jz) | TLN 不否决 |
| 10 | 进程扫描 | — |
| 10 | 状态打印 | — |
| 20 | 模式学习 | — |
| 30 | 代码优化 (死代码删除) | — |
| 50 | NOP 删除 | — |
| 50 | 源文件变更检测 | — |
| 100 | 闲时 MCTS 搜索 | 满足闲时条件 |
| 100 | 进程模式学习 | — |
| 200 | 代码读取 | — |
| 200 | 变异引导推荐 | drive > 0 |
| 800 | 归纳提取 | — |
| 1000 | 持久化 (基础) | — |
| 1000 | Fission | 满足分裂条件 |
| 1000 | 归纳测试 | — |
| 1200 | 归纳应用 | — |
| 2000 | TLN 进化 (0.5% 变异) | tln_enabled |
| 5000 | 持久化 (完整 + TLN) | — |
| 10000 | 规则衰减 | — |

---

## 验证清单

- [ ] 运行引擎 100 轮，观察 `[XXXX]` 前缀的输出，确认各任务按预期间隔触发
- [ ] 修改 `mod_cycle = 10` 为 `mod_cycle = 5`，观察代码修改更频繁
- [ ] 观察 "IDLE cycle" 输出：应在 rounds_since_mod >= 300 后出现
- [ ] 观察 "SNAP: ROLLBACK" 输出：drive 跌到 -20 以下时应触发
- [ ] 通过 socket 提交任务：`echo "task:exec:ls" | socat - UNIX-CONNECT:/tmp/torkd.sock`，然后 `echo "result:1" | socat - UNIX-CONNECT:/tmp/torkd.sock` 查看结果