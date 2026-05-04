# TORK 流水线记录

## 当前状态 (v3.14)

### 已完成
- Phase 1-5: 全部完成
- v3.10: 真实进化引擎 (4次参数调制)
- v3.11: 模式学习回路验证 (3个模式)
- v3.12: 完整学习闭环 (经验→模式→磁盘→加载→行为)
- v3.13: 自调参闭环 (模式→tune params→本能→动态心跳)
- v3.14: π-Heartbeat (时效·共振·指纹·震荡)
- v3.14+: 统一 Tool Dispatch 闭环层 + codegen 代码生成管道

### 核心能力
- asm核心: TOR循环 + 种子存续 + CRC自校验
- C引擎: fork+exec + socket + soul读写 + 快照回滚
- 本能: 恐惧/欲望/好奇心 + 模式感知 + 自调参
- 学习: 经验缓冲区 + MCTS + 模式提取 + 深度回放
- 进化: DeepSeek云端指导 + 参数调制 + 自修改
- **调度: 统一 Tool Dispatch — 所有行为经 tork_dispatch() 发出，结果自动回流 experience**
- **代码生成: 2 模板 + 7 变异策略 + MCTS 搜索 + 编译验证 + benchmark**
- 集群: UDP多播黑板 + 经验交换
- 分布: AppImage 232KB, ELF x86-64

### v3.14+ 架构突破
1. **dispatch 闭环**: 三条执行路径 (torkd/task/idler) 合并为唯一入口 tork_dispatch()
2. **伪经验消除**: tick%3/5/7 假记录已删除，只有真实行为生成 experience
3. **codegen 管道**: TORK 的"吃饭手艺" — 需求→模板→变异→编译→benchmark→返回
4. **10 action 类型**: 4 内部 + 3 外部 + 3 codegen，全部回流 experience
5. **模式学习触发**: 每 10 次 dispatch 成功自动触发 pat_learn_from_experience()

### 验证指标
- 模式学习: 8个模式/980+经验
- dispatch 闭环: dispatch_total_calls() == exp_count增量 (无旁路泄漏)
- codegen: 7/7 变异策略编译通过，MCTS 搜索返回最优变体
- 单元测试: 25/25 通过
- 自调参: curiosity 1.15→2.15 (+87%)