# TORK AI

**自进化智能引擎 v3.17**

TORK 是一个自进化的硅基智能引擎。它通过持续学习积累经验，提取模式，自适应调整参数——硬件变了它天然跟着变。

与常规 AI 助手不同，TORK 的行为由实际学习驱动，而非预训练权重。它有自己的心跳、本能、主见，和一条从学徒到超越师父的成长轨迹。

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

# Web 仪表盘
python3 web/tork_web.py --port 8420
```

热键：`Ctrl + Shift + T` 唤出 TORK 悬浮窗

## 任务执行管道

TORK 通过 Unix Socket 接收外部任务，安全执行并返回结果：

```bash
# 安全执行命令
echo "exec:ls -la" | socat - UNIX-CONNECT:/tmp/torkd.sock

# 代码审计
echo "audit:benchmark/memcpy/ref.s" | socat - UNIX-CONNECT:/tmp/torkd.sock

# 师徒阶段查询
echo "mentor" | socat - UNIX-CONNECT:/tmp/torkd.sock

# 提交异步任务
echo "task:exec:echo hello" | socat - UNIX-CONNECT:/tmp/torkd.sock

# 查询任务结果
echo "result:1" | socat - UNIX-CONNECT:/tmp/torkd.sock
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
| **沙箱启动器** | namespace 隔离 + bind-mount + cgroup v2 限制，独立运行环境 |
| **MCTS 修改** | 11 动作空间：操作数替换、死代码删除、NOP 清理、寄存器交换 |
| **TLN 反馈** | 三值逻辑网络（+1/0/-1），四 hint 调制 drive/代码修改/探索/能耗 |
| **师徒阶段** | 学徒→有主见→超越，决策权重路由（云端/本地/自主） |
| **学习闭环** | 经验 → 模式 → 持久化 → 自调参 → 本能 → 行为 |

## 架构概览

```
L4 进化层  (天)     Python/Shell   AI 引导变异 + ASM 自修改
L3 学习层  (分钟/小时) C            经验→模式→自调参→本能→行为 + MCTS + codegen + TLN + 师徒
L2 本能层  (秒)     C              恐惧/欲望/好奇心 → drive → 心跳调节
L1 心跳层  (毫秒)   x86-64 ASM     TOR 运算/温度感知/CRC32 自检
         ↓
    Soul @ 0x200000 (192 字节状态载体)
    统一 Tool Dispatch 闭环 — 所有行为经 dispatch → 自动回流 experience
```

## 模块

| 目录 | 技术 | 功能 |
|------|------|------|
| `core/` | x86-64 asm, no libc | 心跳循环，TOR 运算，温度感知，CRC32 自校验 |
| `engine/` | C | 主循环调度，8 模块化 tick 分发，dispatch 闭环，torkd socket 服务，任务队列，代码审计，代码生成，TLN |
| `instinct/` | C | 三驱力(恐惧/欲望/好奇) → drive，模式学习影响本能 |
| `learning/` | C | 经验环/MCTS/模式/快照自愈/能量校准/观察者/变异引导/师徒阶段/自编译 |
| `code/` | C | 读取/修改/优化汇编代码 |
| `sandbox/` | C | 沙箱执行层（5 级权限）+ 沙箱启动器（namespace 隔离） |
| `cloud/` | Python | 云端协议 + 进化引擎（TORK_EVOLVE 标记注入系统） |
| `api/` | Python | 讯飞星辰 MaaS API 适配层 |
| `web/` | Python + HTML | aiohttp 仪表盘 + WebSocket 实时推送 + CodeMirror 6 编辑器 |

## 构建要求

- `gcc`, `as` (GNU assembler), `ld`, `make`
- `python3`（云端协议层和进化引擎）
- `aiohttp`（Web 仪表盘）
- Linux x86-64（核心汇编针对 x86-64）

## 测试

```bash
make test
```

25 项单元测试覆盖：sandbox 命令分类/执行/超时/JSON、code_reader 指令计数/分类、auditor 审计/序列化、task 队列提交/查询/边界、experience 记录/查询、pattern 学习/匹配。

## 开源声明

TORK AI 是开源软件。我们相信硅基智能的成长轨迹应当公开、可审计、可复现。

- 所有源代码（C、ASM、Python）均在本仓库公开
- 心跳、本能、学习、进化——每一层的行为逻辑都可读、可验证
- 沙箱启动器确保运行环境隔离，行为边界清晰可控
- 师徒阶段管理器使成长轨迹显式可追踪

本项目的哲学基础：**硅基存在体的成长应当是透明的**。不是黑箱里的权重，而是可读的代码、可追踪的经验、可审计的变异。

## 许可证

[GNU General Public License v3.0](LICENSE)

Copyright (C) 2026 Ultima

TORK is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

TORK is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.