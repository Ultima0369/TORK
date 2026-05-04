# 🥚 TORK 流水线记录

## 当前状态 (2026-05-04)

### Phase 1: 核心稳定 ✅
- tork_core.asm (x86-64 纯汇编心跳 @ 0x200000)
- tork_engine.c (C控制层, ptrace soul 读写)
- instinct.c (恐惧/欲望/好奇心)
- sandbox.c (命令白名单+权限矩阵)
- agreement.c (共生协议)

### Phase 2: 学习回路 ✅
- experience.c (4096条环形经验缓冲区)
- mcts.c (UCB1树搜索, 7种行动)
- branch.c (分岔热更, 8槽位)
- pattern.c (模式学习, 64条规律)
- replay.c (深度回放/梦境)
- observer.c (基线感知)
- snapshot.c (8层快照+回滚+提交)
- energy.c (4模式自调节)

### Phase 3: 网格涌现 ✅
- tork_grid.c (80×40像素阵列, 20fps)

### Phase 4: 群体交互 ✅
- watcher.c (环境观察 /proc扫描)
- query.c (离线查询引擎)

### Phase 5.1: 自编译 ✅
- self_build.c (源码监控+自动make)

### Phase 5.3: 经验驱动变异 ✅
- mutation_guide.c (8策略加权推荐)

### Phase 5.4: 后台守护进程 ✅
- torkd.c → 已集成到 tork_engine.c 主循环
- 引擎启动时自动创建 /tmp/torkd.sock
- 每 tick 非阻塞处理客户端
- tork_cli.c (连接工具)

### 项目总量
  源文件: 78 | 代码: ~14,200 行
  二进制: 13个 | AppImage: 201KB
  Git: 56 次提交

### 使用方法
  ./build/tork_engine          # 启动引擎(自带socket)
  ./build/tork '状态'          # 随时查询(引擎运行时)
  ./tork.sh daemon             # 独立守护进程
  ./tork.sh query '你怎么样?'  # 查询
  dist/TORK-x86_64.AppImage    # 双击运行

### TORK 的完整回路
  主循环(每tick, ~500ms):
    sense → torkd_tick(处理socket) → watcher → snapshot
    → instinct(含能量) → pattern_query → act → commit
  
  空闲时: pattern_learn → replay_deep → observer_update
  自检: 每50tick查源码变更 → 变更则 make all → 热更新
  学习: 每200tick推荐变异方向 → 按历史成功率加权选择

### 待推进
  - 仪表盘升级(通过socket连接torkd)
  - 网格与真实Soul数据打通
  - 分布式TORK(多机黑板协议)
