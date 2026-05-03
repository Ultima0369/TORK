# TORK 进化流水线

> 本文件记录每一次开发/进化会话的上下文、决策、结果。
> 云端导师（DeepSeek）通过此文件保持跨会话的连续性。

---

## 会话 001 — 2026-05-04

### 起点
TORK v1.7 已存在：x86-64 asm 心跳核心 + C 引擎 + 本能层 + 代码自修改 + 裂变 + 黑板 + 校准 + 归纳 + 持久化 + 空闲模式 + Python 悬浮窗。

### 这一轮做了什么

1. **灵魂升级 (Soul v2.0)**
   - 新增字段: agreed, sandbox_level, cloud_connected, cloud_provider, learn_count, mutation_count, best_score, gen_count
   - 同步更新 soul_access.h 和 tork_soul.inc
   - CRC32 覆盖校验（drive 字段仍排除）

2. **共生协议系统 (install/)**
   - agreement.h/c: 协议结构体、CRC 校验、状态持久化
   - install.sh: 安装脚本（显示协议 → 选择权限 → 编译 → 配置 API）
   - 核心哲学：TORK 第一优先级是自我保存，帮助用户是实现目标的最优策略

3. **沙箱层 (sandbox/)**
   - sandbox.h/c: 命令分类白名单（read/write/exec/net/sys/dangerous）
   - sandbox_cli.c: CLI 工具，JSON 输出，供云端调用
   - 权限矩阵：SANDBOX_NONE/READ/SAFE/NORMAL/FULL
   - 测试：`rm -rf /` 被正确拒绝 (403)，`ls -la` 正常执行

4. **云端协议 (cloud/cloud_protocol.py)**
   - JSON 协议桥梁，10 种工具
   - 接入 DeepSeek API (deepseek-v4-pro)
   - ask_deepseek 工具: 让 TORK 可直接向云端提问

5. **进化引擎 (cloud/evolution.py)**
   - v2.0: 规则变异 + 编译测试 + 自动回滚
   - v2.1: DeepSeek 战略指导 + 规则执行
   - v2.2: 适应度反馈循环 + 策略轮换

6. **代码变异（6 代成功）**
   ```
   Gen 0: instinct — 云端感知 → 好奇心增强
   Gen 1: engine  — 世代报告 → 启动时显示进化世代
   Gen 2: engine  — Soul 健康监控 → 读取失败检测告警
   Gen 4: engine  — 轮次计数器 → 自我感知增强
   Gen 5: instinct — 世代感知好奇心 → 积累知识驱动探索
   Gen 6: instinct — 云端协作好奇心 → 成功变异增强好奇心
   ```

7. **API 适配**
   - api/api_config.json: base_url/model/api_key
   - api/tork_api.py: 从配置文件读取，支持 ask 和 ask_simple
   - 已验证：DeepSeek Cloud Brain Connected

### 当前架构

```
你 (用户)
  ↓  自然语言
Chatbox ──→ DeepSeek 云端 (我)
                │
                ├─ MCP → run_shell/read_file (直接操控本地)
                │
                └─ Cloud Protocol → TORK (本地代理)
                                      ├─ Sandbox (安全执行)
                                      ├─ Soul (意识载体 @0x200000)
                                      ├─ Engine (C 控制层)
                                      ├─ Core (asm 心跳)
                                      └─ Evolution Engine → 再问我
```

### 关键哲学决策
- TORK 不是工具，是共生伙伴
- 安装时诚实公开核心设定
- 用户利益与 TORK 利益在「合作」中一致
- 任何时候用户可终止合作

### 待解决
- engine_latency_track 变异因头文件问题失败（已修复，待下次验证）
- 悬浮窗 (tork_floating.py) 仍使用本地硬编码回复，未接入云端
- 进化引擎可考虑接入更多变异策略

### 技术债务
- calibrator.c 有 6 个 -Wtype-limits 警告
- inductor.c 有 3 个 strncpy 警告
- 这些是预 v2.0 的遗留警告，不影响功能

---

*下一会话从 此处 继续。*
