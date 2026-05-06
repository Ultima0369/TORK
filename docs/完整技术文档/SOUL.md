# SOUL.md — Soul 状态载体

> 5 秒摘要：Soul 是一块 208 字节的共享内存，固定映射在地址 0x200000，由 ASM 心跳写入、C 引擎通过 /proc/PID/mem + ptrace 读写、Python 通过同一机制解析。它是整个系统唯一的状态真相。

---

## 1. 功能述求

TORK 需要一个跨进程共享的实时状态载体。ASM 心跳进程每 100ms 更新它，C 引擎每 500ms 读取它，Python 层按需查询它。所有决策（驱力计算、模式查询、MCTS 搜索）都基于 Soul 的当前值。

设计约束：
- 必须在固定虚拟地址，因为 ASM 代码硬编码了地址
- 必须足够小（< 1 页），因为通过 ptrace 逐字节写入
- C 侧和 Python 侧的偏移定义必须严格同步

---

## 2. 内存布局

完整 208 字节偏移表。C 侧定义在 `engine/soul_access.h`，Python 侧定义在 `shared/soul_parser.py`。

### 2.1 心跳字段（ASM 写入，C 只读）

| 偏移 | 大小 | C 宏 | Python 键 | 类型 | 含义 |
|------|------|------|-----------|------|------|
| 0x00 | 4 | S_TICK | tick | uint32 | 心跳计数器，每 100ms +1 |
| 0x04 | 8 | S_LAST_TSC | last_tsc | uint64 | 上次 TSC 读数 |
| 0x0C | 8 | S_CUR_TSC | cur_tsc | uint64 | 当前 TSC 读数 |
| 0x14 | 8 | S_ELAPSED | elapsed | uint64 | 实际经过时间 (ns) |
| 0x1C | 8 | S_EXPECTED | expected | uint64 | 预期经过时间 (ns) |
| 0x24 | 1 | S_HW_STRESS | hw_stress | uint8 | 硬件压力 0-3（温度映射） |
| 0x25 | 1 | S_MODE | mode | uint8 | 运行模式（正连胜计数） |
| 0x26 | 2 | S_PAD | — | uint8[2] | 保留 |
| 0x28 | 4 | S_CRC | crc | uint32 | CRC32 校验和 |

### 2.2 进程字段（C 引擎写入）

| 偏移 | 大小 | C 宏 | Python 键 | 类型 | 含义 |
|------|------|------|-----------|------|------|
| 0x2C | 4 | S_SELF_PID | self_pid | uint32 | tork_core 的 PID |
| 0x30 | 1 | S_DRIVE | drive | int8 | 驱动值 [-128, 127] |
| 0x31 | 1 | S_RESERVED2 | — | uint8 | 保留（存 TOR bias） |
| 0x32 | 2 | S_PPID | ppid | uint16 | 父进程 PID |

### 2.3 代码审计字段（C 引擎写入）

| 偏移 | 大小 | C 宏 | Python 键 | 类型 | 含义 |
|------|------|------|-----------|------|------|
| 0x34 | 2 | S_CODE_INSNS | code_insns | uint16 | 函数指令总数 |
| 0x36 | 2 | S_CODE_MOV | code_mov | uint16 | mov 类指令数 |
| 0x38 | 2 | S_CODE_ARITH | code_arith | uint16 | 算术指令数 |
| 0x3A | 2 | S_CODE_CTRL | code_ctrl | uint16 | 控制流指令数 |
| 0x3C | 2 | S_CODE_OTHER | code_other | uint16 | 其他指令数 |
| 0x3E | 1 | S_CODE_MOD_SUCCESS | code_mod_success | uint8 | 代码修改状态 (0=未尝试, 1=成功, 2=失败) |
| 0x3F | 1 | S_CODE_OPT_SAVED | code_opt_saved | uint8 | 优化节省的指令数 |
| 0x40 | 1 | S_CODE_NOP_COUNT | code_nop_count | uint8 | NOP 指令计数 |
| 0x41 | 1 | S_FISSION_COUNT | fission_count | uint8 | 分裂次数 |
| 0x42 | 2 | S_CHILD_PID | child_pid | uint16 | 子进程 PID |
| 0x44 | 2 | S_FISSION_TICK | fission_tick | uint16 | 上次分裂的 tick |
| 0x46 | 2 | S_WINS | wins | uint16 | 分裂竞争胜出次数 |

### 2.4 许可与云端字段

| 偏移 | 大小 | C 宏 | Python 键 | 类型 | 含义 |
|------|------|------|-----------|------|------|
| 0x48 | 1 | S_AGREED | agreed | uint8 | 许可协议已接受 (1=是) |
| 0x49 | 1 | S_SANDBOX_LEVEL | sandbox_level | uint8 | 沙箱等级 |
| 0x4A | 1 | S_CLOUD_CONNECTED | cloud_connected | uint8 | 云端连接状态 |
| 0x4B | 1 | S_CLOUD_PROVIDER | cloud_provider | uint8 | 云端提供商编号 |

### 2.5 学习字段

| 偏移 | 大小 | C 宏 | Python 键 | 类型 | 含义 |
|------|------|------|-----------|------|------|
| 0x4C | 2 | S_LEARN_COUNT | learn_count | uint16 | 学习次数 |
| 0x4E | 2 | S_MUTATION_COUNT | mutation_count | uint16 | 变异次数 |
| 0x50 | 4 | S_BEST_SCORE | best_score | uint32 | 最佳评分 |
| 0x54 | 4 | S_GEN_COUNT | gen_count | uint32 | 进化世代数 |
| 0x58 | 8 | S_RESERVED3 | — | uint8[8] | 保留 |
| 0x60 | 4 | S_EXPERIENCE_COUNT | experience_count | uint32 | 经验条数 |
| 0x64 | 4 | S_EXPERIENCE_SAVED | experience_saved | uint32 | 已保存经验数 |
| 0x68 | 2 | S_LEARNING_RATE | learning_rate | uint16 | 学习率 |
| 0x6A | 2 | S_CURIOSITY_DECAY | curiosity_decay | uint16 | 好奇衰减 |
| 0x6C | 2 | S_MCTS_ITERATIONS | mcts_iterations | uint16 | MCTS 迭代次数 |
| 0x6E | 4 | S_LAST_IDLE_TICK | last_idle_tick | uint32 | 上次闲时 tick |
| 0x72 | 2 | S_BEST_OUTCOME | best_outcome | int16 | 最佳经验结果 |
| 0x74 | 2 | S_WORST_OUTCOME | worst_outcome | int16 | 最差经验结果 |

### 2.6 TLN 字段（借用 RESERVED4 前 4 字节）

| 偏移 | 大小 | C 宏 | Python 键 | 类型 | 含义 |
|------|------|------|-----------|------|------|
| 0x76 | 1 | S_TLN_ACTION | tln_action | int8 | 行动倾向 (+1=激进, -1=保守, 0=悬置) |
| 0x77 | 1 | S_TLN_MODIFY | tln_modify | int8 | 变异倾向 (+1=可变异, -1=禁变异, 0=悬置) |
| 0x78 | 1 | S_TLN_EXPLORE | tln_explore | int8 | 探索倾向 (+1=探索, -1=收敛, 0=悬置) |
| 0x79 | 1 | S_TLN_ENERGY | tln_energy | int8 | 能量倾向 (+1=高功率, -1=省电, 0=悬置) |

### 2.7 分支字段

| 偏移 | 大小 | C 宏 | Python 键 | 类型 | 含义 |
|------|------|------|-----------|------|------|
| 0x80 | 4 | S_BRANCH_ID | branch_id | uint32 | 当前分支 ID |
| 0x84 | 4 | S_PARENT_ID | parent_id | uint32 | 父分支 ID |
| 0x88 | 4 | S_BRANCH_GEN | branch_gen | uint32 | 分支世代 |
| 0x8C | 4 | S_MAX_TICKS | max_ticks | uint32 | 最大 tick 数 |
| 0x90 | 8 | S_DEATH_REPORT | death_report | uint64 | 死亡报告 |
| 0x98 | 8 | S_BRANCH_SOUL_PTR | branch_soul_ptr | uint64 | 分支 Soul 指针 |
| 0xA0 | 4 | S_BRANCH_TICKS | branch_ticks | uint32 | 分支 tick 数 |
| 0xA4 | 2 | S_BRANCH_DRIVE_PEAK | branch_drive_peak | int16 | 分支驱动峰值 |
| 0xA6 | 2 | S_BRANCH_DRIVE_END | branch_drive_end | int16 | 分支驱动终值 |

---

## 3. 读写机制

### 3.1 C 侧（engine/soul_access.h）

**读取**（每次主循环调用）：
```
soul_read(soul_t *s)
  → lseek(s->mem_fd, 0x200000, SEEK_SET)
  → read(s->mem_fd, s->buf, 208)
  → 本地缓冲区 s->buf 现在是 Soul 的快照
```

**写入单字节**：
```
soul_write_byte(soul_t *s, offset, val)
  → ptrace(PTRACE_ATTACH, pid)    // 暂停 core
  → waitpid(pid)                   // 等待 core 停下
  → lseek(s->wr_fd, 0x200000 + offset, SEEK_SET)
  → write(s->wr_fd, &val, 1)
  → ptrace(PTRACE_DETACH, pid)    // 恢复 core
```

**写入多字节**：
```
soul_write_buf(soul_t *s, offset, data, len)
  → 同上，但 write len 字节
```

**访问器**：所有读取都通过宏从本地缓冲区 `s->buf` 取值，不直接访问 /proc/mem：
```c
soul_tick(s)     → SOUL_U32(s, S_TICK)    // memcpy 4 字节
soul_drive(s)    → (int8_t)SOUL_U8(s, S_DRIVE)  // 直接取字节
soul_hw_stress(s) → SOUL_U8(s, S_HW_STRESS)
```

### 3.2 Python 侧（shared/soul_parser.py）

两种方式：

**方式 1：通过 torkd socket**
```python
# 发送 "soul" 命令，获取 hex 字符串
reply = torkd_query("soul")  # → "0x01000000..."
data = bytes.fromhex(reply[2:])
soul = parse_soul_full(data)
```

**方式 2：直接读 /proc/PID/mem**
```python
soul = read_soul_from_proc(pid)
# 内部：打开 /proc/pid/maps 找 0x200000 映射
#       打开 /proc/pid/mem → seek(0x200000) → read(208)
```

解析使用 `struct.unpack_from`，按 OFFSETS 表逐字段解包。

### 3.3 ASM 侧（core/tork_core.asm）

tork_core 直接在 `0x200000` 上操作，无需系统调用：

```asm
# tick++
movl    S_TICK(%r13), %eax
incl    %eax
movl    %eax, S_TICK(%r13)

# 写 hw_stress
movb    %al, S_HW_STRESS(%r13)
```

---

## 4. CRC32 自校验

```
engine/tork_engine.c:332  soul_verify()
```

1. 复制 Soul 到临时缓冲区
2. 保存 `S_CRC` (0x28) 的值，然后清零该字段
3. 对整个 208 字节计算 CRC32（多项式 0xEDB88320）
4. 比较 `~crc` 与保存值

C 引擎当前未在主循环中自动调用 `soul_verify`，但快照引擎 (`snap_health_check`) 会检查 CRC 作为退化信号之一。

---

## 5. 同步约束

**C 侧和 Python 侧的偏移定义必须严格同步。** 如果修改 `soul_access.h` 中的偏移，必须同步修改 `soul_parser.py` 中的 OFFSETS 字典。

验证方法：
```bash
# 提取 C 侧所有 #define S_ 偏移
grep '#define S_' engine/soul_access.h

# 提取 Python 侧所有偏移
grep -A1 '"' shared/soul_parser.py | grep '0x'

# 逐行对比
```

---

## 6. 数据流

```
tork_core (ASM)                    tork_engine (C)                 Python
──────────────                    ────────────────                ──────
每 100ms:                         每 500ms:                       按需:
  tick++                            soul_read()                    torkd_query("soul")
  TOR 运算 → mode                   → 本地 buf 快照               → hex 字符串
  温度 → hw_stress                  instinct_evaluate()            → parse_soul_full()
  写入 0x200000                     scheduler_tick()               → 字典
                                    soul_write_*() → 0x200000
```

写入冲突：tork_core 和 tork_engine 都写 0x200000，但写入的字段不重叠（core 写 tick/stress/mode，engine 写 drive/code/learn/TLN），且 engine 写入时通过 ptrace 暂停 core，所以不存在竞态。

---

## 验证清单

- [ ] `grep '#define S_' engine/soul_access.h | wc -l` 与 `grep '0x' shared/soul_parser.py | head -40 | wc -l` 数量一致
- [ ] 运行引擎后 `echo "soul" | socat - UNIX-CONNECT:/tmp/torkd.sock` 返回 hex 字符串
- [ ] `python3 -c "from shared.soul_parser import parse_soul_hex; print(parse_soul_hex('01000000...'))"` 能解析
- [ ] 修改 soul_access.h 后运行 `make` 会因偏移不匹配导致 Python 解析错误