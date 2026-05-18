# TORK 项目索引

> 📅 最后更新: 2026-05-18 · 核心源码 ~3MB
>
> 非核心模块已移至 `archive/bak_20260518/`（Python 桥接、GUI、集群部署等）
> 构建产物已清理，`make` 重新生成

---

## 目录结构

```
0EGG/
├── src/            ★★★★★ 核心源码（~65 个 C 文件）
│   ├── engine/     引擎核心
│   ├── learning/   学习系统
│   ├── code/       自修改代码
│   ├── crypto/     加密模块
│   ├── network/    网络协议
│   ├── bridge/     外部桥接
│   ├── mesh/       车际网格
│   ├── edge/       边缘部署
│   ├── nn/         神经网络 (THEIA)
│   ├── slime/      黏菌算法
│   ├── rollback/   回滚系统
│   ├── sandbox/    沙箱
│   └── compat/     Windows 兼容层
├── mk/             ★★★★  构建系统（7 个 mk 文件）
├── docs/           ★★★★  项目文档
├── tests/          ★★★   测试代码（10 个测试文件）
├── unit_tests/     ★★★   Unity 测试框架
├── persist/        ★★★   运行时数据
├── archive/        ★      历史归档
│   └── bak_20260518/      非核心模块备份
├── build/          ★      构建产物（空，make 生成）
├── Makefile        ★★★★★ 主构建入口
├── Makefile.cross  ★★★   交叉编译 (MinGW)
├── Makefile.edge   ★★★   边缘设备编译 (ARM)
├── Dockerfile      ★★★   容器化
├── docker-compose.yml ★★  集群编排
└── .github/        ★★    CI 工作流
```

---

## 核心源码分组

| 目录 | 模块数 | 重要度 | 说明 |
|:----|:----:|:------:|:-----|
| `src/engine/` | 42 | ★★★★★ | 引擎核心：心跳、灵魂、调度、TLN、torkd、日志、看门狗 |
| `src/learning/` | 20 | ★★★★ | 学习系统：MCTS/TLN/模式/能量/经验回放/分析 |
| `src/code/` | 3 | ★★★★ | 自修改代码：读/改/校验 |
| `src/crypto/` | 4 | ★★★★ | 加密：SHA256 + HMAC 流加密 |
| `src/rollback/` | 2 | ★★★★★ | 回滚系统：5 版本回溯 + 自动恢复 |
| `src/network/` | 4 | ★★★ | HTTP + WebSocket + Webhook |
| `src/bridge/` | 4 | ★★★ | 外部桥接（SP/WebSocket/BitNet/集成） |
| `src/mesh/` | 3 | ★★★ | 车际网格（T2T + 健康交换 + HMAC） |
| `src/edge/` | 6 | ★★★ | 边缘部署（传感器/预测/时间/温度/部署脚本） |
| `src/nn/` | 4 | ★★★★ | THEIA 神经网络（论文复刻 + Hebbian 学习） |
| `src/slime/` | 2 | ★★★★ | 黏菌算法（空间优化 + 工作流编排） |
| `src/sandbox/` | 2 | ★★ | 沙箱 |
| `src/compat/` | 2 | ★★ | Windows POSIX 兼容层 |

---

## 测试文件清单

| 文件 | 类型 | 覆盖模块 |
|:-----|:----|:---------|
| `test_engine.c` | 单元测试 | tork_context.h |
| `test_learning.c` | 单元测试 | pattern 模块 |
| `test_core.c` | 集成测试 | 核心心跳循环 |
| `test_pbft.c` | 单元测试 | PBFT 共识 |
| `test_theia_paper.c` | 集成测试 | THEIA 神经网络 |
| `test_tork_mesh.c` | 集成测试 | T2T Mesh |
| `test_edge_predictor.c` | 集成测试 | 边缘预测 |
| `test_tln.c` | 单元测试 | TLN 三值逻辑 |
| `test_instinct.c` | 集成测试 | 本能系统 |
| `test_blackboard.c` | 集成测试 | 黑板报 |

---

## 提交历史（最新 10 条）

```
ced835d  P0全局变量收编: tork_context.h + 测试 + 文档更新
853a041  THEIA 论文架构复刻: 模块化 K3 三值神经网络
89eca56  TORK BitNet b1.58 bridge: 三值大脑直连
21c97ee  THEIA + Slime Mold + Windows compat: 技术雕刻三模块
e2c5a51  车际网格 (T2T Mesh)
e3a6d1b  TORK Edge: 边缘部署 + 智能预警系统
c6db024  存在宣言 + 真需求场景 + 开源契约
8827cf4  Phase 8: 安全审计 P0修复 (HMAC + 加密 + 看门狗)
d086a40  P1盲点修复: NTP时间同步 + 温度回退 + CRC奇偶校验
7b973c6  dual-license: 核心汇编专有化
```

---

## 快速入口

| 你想做什么 | 看什么文件 |
|:----------|:----------|
| 理解 TORK 灵魂 | `src/engine/soul_access.h` |
| 看主循环 | `src/engine/tork_engine.c` |
| 全局上下文 | `src/engine/tork_context.h` |
| 改心跳频率 | `src/config.h` |
| 加新功能 | `mk/` 加一行 `.o` 规则 |
| 检查健康 | `src/engine/tork_health.h` |
| 连网抓数据 | `src/network/tork_http.h` |
| 数据分析 | `src/learning/tork_analytics.h` |
| 通信协议 | `src/engine/torkd.h` |
| 神经网络 | `src/nn/theia_paper.h` |
| 黏菌算法 | `src/slime/slime_mold.h` |
