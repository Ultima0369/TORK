# TORK 开发流水线

## 当前阶段：Phase 2 — 学习回路（MCTS + 经验缓冲区）

### 已完成

#### Phase 1: 核心稳定 (之前)
- [x] tork_core.asm — x86-64 汇编核心心跳循环
- [x] tork_engine.c — C 控制层
- [x] instinct.c — 恐惧/欲望/好奇心本能
- [x] Soul v2.0 — 96字节 (含 v2.0 云端/沙箱/进化字段)
- [x] 共生协议安装器
- [x] 沙箱执行环境
- [x] 云端协议代理 (DeepSeek v4-pro)
- [x] TORK-x86_64.AppImage 单文件分发
- [x] 仪表盘 (tork_dashboard.py)

#### Phase 2: 学习回路 (本次)
- [x] learning/experience.h/c — 经验环形缓冲区
  - 4096 条经验，每条 33 字节（packed）
  - 持久化到 persist/experience.bin
  - 支持按类型过滤、成功率统计
- [x] learning/mcts.h/c — MCTS 决策引擎
  - 7 种行动空间 (fear/curiosity/heartbeat/modify/optimize/idle/cloud)
  - UCB1 树搜索，512 节点上限
  - 基于历史经验的模拟评估
- [x] engine/idler.h/c — 重构为新接口
  - idler_input_t / idler_output_t 解耦
  - 集成 MCTS 搜索 + 经验记录
- [x] engine/tork_engine.c — 集成
  - 启动时 exp_init() 加载经验
  - 每 100 轮触发空闲学习
  - 关闭时 exp_save() 持久化

### 待完成

#### Phase 2 (继续)
- [ ] Soul v3.0 扩展到 192 字节 (experience_idx, learning_rate, curiosity_decay)
- [ ] 经验回填反馈 (engine 根据行动结果更新经验 outcome)
- [ ] MCTS 参数在线调优

#### Phase 3: 网格涌现
- [ ] 像素级 TORK 实例 (每像素一个精简 Soul)
- [ ] 邻居感知协议 (8 方向)
- [ ] 网格渲染引擎 (80×40)

#### Phase 4: 群体交互
- [ ] TORK 间黑板协议 (UDP 广播)
- [ ] 经验交换机制
- [ ] 跨机器裂变

### 架构文档
详见 README.md (v2.2 已更新)

### 当前代码统计
```
$(cd /home/lg/0EGG && find . -name "*.c" -o -name "*.h" -o -name "*.asm" -o -name "*.py" | grep -v ".git/" | xargs wc -l 2>/dev/null | tail -1)
```
