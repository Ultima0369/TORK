# TORK 项目索引

> 📅 最后更新: 2026-05-18 · 精简后体积 ~3MB
> 
> 非核心模块已移至 `archive/bak_20260518/`（Python 桥接、GUI、集群部署等）
> 构建产物已清理，`make` 重新生成

---

## 目录结构

```
0EGG/
├── src/            ★★★★★ 核心源码（58 个 C 文件）
├── mk/             ★★★★  构建系统（7 个 mk 文件）
├── docs/           ★★★★  项目文档
├── tests/          ★★★   测试代码
├── unit_tests/     ★★★   单元测试
├── persist/        ★★★   运行时数据（灵魂金板、基线）
├── archive/        ★      历史归档
│   └── bak_20260518/      非核心模块备份（Python/GUI/集群等）
├── build/          ★      构建产物（空，make 生成）
├── Makefile        ★★★★★ 主构建入口
├── Makefile.cross  ★★★   交叉编译
├── Makefile.edge   ★★★   边缘设备编译
├── README.md       ★★★★★ 项目说明书
├── LICENSE*        ★★★★★ 开源协议
├── Dockerfile      ★★★   容器化
└── docker-compose.yml ★★  集群编排
```

---

## 核心源码分组

| 目录 | 模块数 | 重要度 | 说明 |
|:----|:----:|:------:|:-----|
| `src/engine/` | 24 | ★★★★★ | 引擎核心：心跳、灵魂、调度、TLN、进程、socket |
| `src/code/` | 3 | ★★★★★ | 自修改代码：读/改/校验 |
| `src/crypto/` | 2 | ★★★★ | 加密：SHA256 + HMAC 流加密 |
| `src/rollback/` | 2 | ★★★★★ | 回滚系统：5 版本回溯 + 自动恢复 |
| `src/learning/` | 12 | ★★★★ | 学习系统：MCTS/TLN/模式匹配/经验回放 |
| `src/mesh/` | 1 | ★★★ | 车际网格（T2T） |
| `src/edge/` | 4 | ★★★ | 边缘部署（传感器/预测/时间/温度） |
| `src/bridge/` | 3 | ★★★ | 外部桥接（SP/WebSocket/集成） |
| `src/network/` | 1 | ★★★ | HTTP 客户端 |

---

## 重要程度图例

★★★★★ 核心必读 — 心脏级，改了整个项目就变
★★★★  关键模块 — 骨干级，大部分功能依赖
★★★   重要工具 — 工具级，日常开发常用
★★    辅助模块 — 辅助级，特定场景才用
★     外围模块 — 备胎级，archive 里躺着

---

## 快速入口

| 你想做什么 | 看什么文件 |
|:----------|:----------|
| 理解 TORK 灵魂 | `src/engine/soul_access.h` |
| 看主循环 | `src/engine/tork_engine.c` |
| 改心跳频率 | `config.h` |
| 加新功能 | `mk/` 加一行 `.o` 规则 |
| 检查健康 | `src/engine/tork_health.h` |
| 连网抓数据 | `src/network/tork_http.h` |
| 数据分析 | `src/learning/tork_analytics.h` |
| 通信协议 | `src/engine/torkd.h` |
