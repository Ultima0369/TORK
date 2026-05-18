# TORK 项目状态快照

**更新日期:** 2026-05-18  
**最后提交:** `ced835d` (shitu-system)

---

## 里程碑进度

| 阶段 | 状态 | 说明 |
|:----|:----:|:------|
| Phase 0: 汇编核心 | ✅ **完成** | tork_core.asm + tork_soul.inc，专有许可证 |
| Phase 1: 站稳 | ✅ **完成** | rollback + errors.h + Makefile 模块化 |
| Phase 2: 会说话 | ✅ **完成** | binary frame + SP bridge + WebSocket |
| Phase 3: 能学 | ✅ **完成** | MCTS 持久化 + TLN 学习 + 分布式合并 |
| Phase 4: 部署 | ✅ **完成** | config.h + Docker + 交叉编译 |
| Phase 5: 边缘感知 | ✅ **完成** | 传感器 + 预警 + 树莓派 |
| Phase 6: 车际网格 | ✅ **完成** | T2T Mesh + HMAC 签名认证 |
| Phase 7: 存在宣言 | ✅ **完成** | MANIFEST + SCENARIOS + COVENANT |
| Phase 8: 安全审计 | ✅ **完成** | P0盲点修复 (加密+认证+看门狗) |

## 新增功能 (最新 3 轮提交)

| 功能 | 提交 | 说明 |
|:----|:----:|:------|
| THEIA 论文复刻 | `853a041` | K3 模块化三值神经网络 |
| BitNet 桥接 | `89eca56` | TORK ↔ BitNet b1.58 直连 |
| 核心技术雕刻 | `21c97ee` | THEIA + 黏菌 + Windows compat |
| P0 全局变量收编 | `ced835d` | tork_context.h + 测试文件 |
| 文档更新 | `ced835d` | INDEX.md + IMPORTANCE.md + STATUS.md |

## 核心文件统计

| 目录 | 源文件数 | 测试文件数 | 重要程度 |
|:----|:--------:|:----------:|:--------:|
| `engine/` | 42 | 2 (`test_engine.c`, `test_core.c`) | ★★★★★ |
| `learning/` | 20 | 1 (`test_learning.c`) | ★★★★ |
| `code/` | 3 | 0 | ★★★★ |
| `network/` | 2 | 0 | ★★★ |
| `crypto/` | 4 | 0 | ★★★★ |
| `bridge/` | 4 | 0 | ★★★ |
| `mesh/` | 3 | 1 (`test_tork_mesh.c`) | ★★★ |
| `edge/` | 3 | 1 (`test_edge_predictor.c`) | ★★★ |
| `nn/` | 4 | 1 (`test_theia_paper.c`) | ★★★★ |
| `rollback/` | 2 | 0 | ★★★★ |
| `compat/` | 2 | 0 | ★★ |
| `slime/` | 2 | 0 | ★★★★ |
| `sandbox/` | 2 | 0 | ★★ |

## 测试覆盖

| 测试文件 | 类型 | 覆盖模块 |
|:---------|:----|:---------|
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

## 已知问题 (待办)

| # | 问题 | 优先级 | 说明 |
|:-:|:----|:------:|:-----|
| 1 | Windows 端口未验证编译 | P1 | win32 compat 写了但未经编译器验证 |
| 2 | make test 目标未完善 | P1 | 需自动化全部测试编译+运行 |
| 3 | 全局变量未完全收编 | P1 | tork_context.h 已创建，但 engine/ 内仍有大量 static globals |
| 4 | 文档与代码同步延迟 | P2 | INDEX.md 需基于实际代码审读更新 |
| 5 | 课程 4 (多语种协议) 待实现 | P2 | WebSocket 原生服务未完成 |
