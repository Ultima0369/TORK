# TORK AI

**自进化智能引擎 v3.14**

TORK AI 是一个自进化的智能代码助手。它通过持续学习你的编码模式，提供上下文感知的代码建议、审计和优化。

与常规 AI 助手不同，TORK 的行为由实际学习驱动——它从每次执行中积累经验，提取模式，自适应调整参数，硬件变了它天然跟着变。

（如果你想了解它的底层架构——四层循环、ASM 心跳、进化引擎——请参阅 [技术架构文档](docs/architecture.md)）

---

## 快速开始

```bash
# 构建
make all

# 启动引擎
./tork.sh start

# 查看状态
./tork.sh status

# 配置云端 API Key（可选，启用 AI 引导进化）
./tork.sh connect
```

热键：`Ctrl + Shift + T` 唤出 TORK 悬浮窗

## 任务执行管道

TORK 通过 Unix Socket 接收外部任务，安全执行并返回结果：

```bash
# 安全执行命令
echo "exec:ls -la" | socat - UNIX-CONNECT:/tmp/torkd.sock

# 代码审计
echo "audit:benchmark/memcpy/ref.s" | socat - UNIX-CONNECT:/tmp/torkd.sock

# 提交异步任务
echo "task:exec:echo hello" | socat - UNIX-CONNECT:/tmp/torkd.sock
echo "task:audit:benchmark/memcpy/ref.s" | socat - UNIX-CONNECT:/tmp/torkd.sock

# 查询任务结果
echo "result:1" | socat - UNIX-CONNECT:/tmp/torkd.sock

# 任务队列状态
echo "tasks" | socat - UNIX-CONNECT:/tmp/torkd.sock
```

## 进化

```bash
# 单次进化
python3 cloud/evolution.py --rounds 3

# 持续进化模式
python3 cloud/evolution.py --loop --interval 300

# ASM 级自修改（6种变异策略，5轮自动重试）
./tools/asm_evolve.sh
```

进化引擎会：评估自身代码 → 请求 AI 改进建议 → 应用变异 → 编译测试 → 通过则保留，失败自动回滚

## 核心能力

| 能力 | 说明 |
|------|------|
| **代码审计** | 死代码检测、NOP 冗余、指令分布分析、风险评分 |
| **代码生成** | 7 种变异策略 × MCTS 搜索最优汇编变体 + 正确性验证 |
| **沙箱执行** | 5 级权限矩阵、固定缓冲区、零 malloc、命令白名单 |
| **MCTS 修改** | 11 动作空间：操作数替换、死代码删除、NOP 清理、寄存器交换 |
| **学习闭环** | 经验 → 模式 → 磁盘持久化 → 自调参 → 本能 → 行为 |

## 架构概览

```
L4 进化层  (天)     Python/Shell   AI 引导变异 + ASM 自修改
L3 学习层  (分钟/小时) C            经验→模式→自调参→本能→行为 + MCTS + codegen
L2 本能层  (秒)     C              恐惧/欲望/好奇心 → drive → 心跳调节
L1 心跳层  (毫秒)   x86-64 ASM     TOR 运算/温度感知/CRC32 自检
         ↓
    Soul @ 0x200000 (192 字节状态载体)
    统一 Tool Dispatch 闭环 — 所有行为经 dispatch → 自动回流 experience
```

完整架构细节见 [docs/architecture.md](docs/architecture.md)。

## 模块

| 目录 | 技术 | 功能 |
|------|------|------|
| `core/` | x86-64 asm, no libc | 心跳循环，TOR 运算，温度感知，CRC32 自校验 |
| `engine/` | C | 主循环调度，dispatch 闭环，torkd socket 服务，任务队列，代码审计，代码生成 |
| `instinct/` | C | 三驱力(恐惧/欲望/好奇) → drive，模式学习影响本能 |
| `learning/` | C | 经验环(4096)/MCTS(11动作)/模式(64槽)/快照自愈/能量校准/观察者/变异引导 |
| `code/` | C | 读取/修改/优化汇编代码 |
| `sandbox/` | C | 沙箱执行层（5 级权限，固定缓冲区，零 malloc） |
| `cloud/` | Python | 云端协议 + 进化引擎 |
| `api/` | Python | TORK API 适配层 |

## 构建要求

- `gcc`, `as` (GNU assembler), `ld`, `make`
- `python3`（云端协议层和进化引擎）
- Linux x86-64（核心汇编针对 x86-64）

## 测试

```bash
make test
```

25 项单元测试覆盖：sandbox 命令分类/执行/超时/JSON、code_reader 指令计数/分类、auditor 审计/序列化、task 队列提交/查询/边界、experience 记录/查询、pattern 学习/匹配。

## 许可证

MIT License