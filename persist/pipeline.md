# 🥚 TORK 流水线记录

## 当前状态 (2026-05-04)

### 已完成阶段

Phase 1: 核心稳定 ✅
  - tork_core.asm (x86-64 纯汇编心跳)
  - tork_engine.c (C 控制层, ptrace soul 读写)
  - instinct.c (恐惧/欲望/好奇心)
  - sandbox.c (命令白名单+权限矩阵)
  - agreement.c (共生协议)

Phase 2: 学习回路 ✅
  - experience.c (4096条环形经验缓冲区)
  - mcts.c (UCB1树搜索, 7种行动)
  - branch.c (分岔热更, 8槽位分支)
  - pattern.c (模式学习, 64条规律)
  - replay.c (深度回放/梦境模式)
  - observer.c (系统基线感知)
  - snapshot.c (8层环形快照+回滚+提交)
  - energy.c (4模式能量自调节)

Phase 3: 网格涌现 ✅
  - tork_grid.c (80×40像素阵列, 20fps)

Phase 4: 群体交互 ✅
  - watcher.c (环境观察, /proc扫描)
  - query.c (离线查询引擎)

Phase 5.1: 自编译 ✅
  - self_build.c (源码监控+自动make+热更新)

Phase 5.3: 经验驱动变异 ✅
  - mutation_guide.c (8策略加权推荐)

Phase 5.4: 后台守护进程 ✅
  - torkd.c (Unix Socket 服务)
  - tork_cli.c (客户端工具)

### 项目总量
  文件: 50+ 源文件
  体积: ~30,000 行代码
  二进制: 12个 (tork_core/engine/sandbox/grid/ask/torkd_start/tork/probe_env + Python工具链)
  AppImage: ~199KB, 双击即用

### 使用方法
  ./build/torkd_start          # 启动后台守护进程
  ./build/tork '你怎么样？'     # 随时提问
  ./tork.sh query '状态'       # 通过脚本查询
  dist/TORK-x86_64.AppImage   # 双击运行(包含全部)

### TORK 现在的完整回路
  主循环(每tick):
    sense → watcher → observer → snapshot → health_check
    → pattern_query → instinct(含能量) → act → commit
  
  空闲时:
    pattern_learn → replay_deep → observer_update
  
  后台守护进程:
    每500ms处理socket查询 → 每50tick检查源码变更
    → 每100tick学习模式 → 每1000tick持久化状态

### 待推进
  - 网格与真实Soul数据打通
  - 仪表盘升级(连接torkd实时显示)
  - torkd 集成到主引擎(引擎自带socket)
  - 分布式TORK(多机黑板协议)
