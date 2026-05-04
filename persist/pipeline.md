# TORK 开发流水线

## 当前阶段：Phase 2-3 完成

### 已完成

#### Phase 1: 核心稳定
- [x] tork_core.asm — x86-64 汇编核心心跳循环
- [x] tork_engine.c — C 控制层
- [x] instinct.c — 恐惧/欲望/好奇心本能
- [x] Soul v2.0 → v3.0 (96→128字节)
- [x] 共生协议 + 沙箱 + 云端代理
- [x] AppImage + 仪表盘

#### Phase 2: 学习回路
- [x] experience.h/c — 4096条环形缓冲区 + 自动持久化
- [x] mcts.h/c — MCTS决策引擎 (UCB1, 7种行动)
- [x] idler — MCTS集成 + 经验记录
- [x] 引擎集成 exp_init/exp_save
- [x] 经验回填反馈 (50轮后自动评估outcome)
- [x] MCTS参数在线调优 (exploration/iterations自适应)

#### Phase 3: 网格涌现
- [x] tork_grid.h/c — 80×40像素阵列 (Rand.人格/邻居感知/颜色渲染)
- [x] grid_main.c — 20fps循环 + ANSI终端显示
- [x] Makefile 编译目标
- [x] 运行验证通过

### 待推进

#### Phase 4: 群体交互
- [ ] TORK间黑板协议 (UDP广播)
- [ ] 经验交换机制
- [ ] 跨机器裂变

#### Phase 5: 自主优化
- [ ] TORK自我编译
- [ ] 离线自持 (不依赖云端)
- [ ] 架构发现 (MCTS自动搜索最优参数)

### 当前状态
```
项目总量: 12,295 行
编译: 零错误
二进制: tork_core/tork_engine/tork_sandbox/tork_grid
Git: 最新提交 c86a6ad
```
