# SERVICES.md — 外部接口

> 5 秒摘要：TORK 通过 4 个外部接口与外界交互：torkd Unix Socket（命令协议）、Web 仪表盘（aiohttp + WebSocket + CodeMirror 6）、云端 API（讯飞星辰 MaaS + 进化引擎）、Grid 共享内存（终端可视化）。Fission 分裂机制通过 fork 子进程实现竞争进化。

---

## 1. torkd Unix Socket

### 1.1 功能述求

外部程序需要与运行中的 TORK 引擎交互——查询状态、执行命令、提交任务。torkd 是一个非阻塞的 Unix Socket 服务器，每 tick 接受并处理所有待处理连接。

### 1.2 服务器配置

`engine/torkd.c:57-95` (`torkd_init`)

```
路径:     /tmp/torkd.sock
类型:     AF_UNIX, SOCK_STREAM
权限:     0600 (仅 owner 读写)
Backlog:  TORKD_BACKLOG
模式:     非阻塞 (O_NONBLOCK)
超时:     客户端读写各 2 秒 (SO_RCVTIMEO / SO_SNDTIMEO)
```

### 1.3 命令协议

`engine/torkd.c:114-303` (`handle_client`)

客户端发送一行文本（`\n` 结尾），服务端返回一行文本（`\n` 结尾）后关闭连接。

| 命令 | 格式 | 返回 | 说明 |
|------|------|------|------|
| `ping` | `ping` | `pong` | 心跳检测 |
| `status` | `status` 或 `状态` | 多行文本 | 引擎状态摘要 |
| `soul` | `soul` 或 `灵魂` | `0x` + hex 字符串 | Soul 208 字节原始 dump |
| `exec:` | `exec:<command>` | 命令输出或 JSON 错误 | 通过沙箱执行命令 |
| `audit:` | `audit:<path>[:func]` | 审计结果 | 审计 ASM 文件 |
| `codegen:` | `codegen:<action>:<template>` | JSON 结果 | 代码生成管道 |
| `task:` | `task:<type>:<input>` | `{"task_id":N,"status":"pending"}` | 提交异步任务 |
| `result:` | `result:<task_id>` | JSON 状态 + 输出 | 查询任务结果 |
| `tasks` | `tasks` | JSON 队列统计 | 任务队列概览 |
| `exit` / `quit` | `exit` | `bye` | 关闭连接 |
| 其他 | 任意文本 | query_handle() 结果 | 自然语言查询 |

### 1.4 exec: 命令

`engine/torkd.c:160-167`

```
exec:ls -la
→ dispatch_quick(DISP_EXEC_CMD, "ls -la", NULL)
→ sandbox_exec("ls -la")
→ 返回命令输出
```

空命令返回 `{"error":"empty command"}`。

### 1.5 audit: 命令

`engine/torkd.c:168-192`

```
audit:benchmark/memcpy/ref.s:memcpy_tork
→ dispatch_quick(DISP_AUDIT_CODE, "benchmark/memcpy/ref.s", "memcpy_tork")
→ 返回审计结果

audit:benchmark/memcpy/ref.s
→ dispatch_quick(DISP_AUDIT_CODE, "benchmark/memcpy/ref.s", NULL)
```

冒号分隔文件路径和函数名。函数名可选。

### 1.6 codegen: 命令

`engine/torkd.c:193-226`

```
codegen:search:memcpy_byte_loop
→ dispatch_quick(DISP_CODEGEN_SEARCH, "memcpy_byte_loop", NULL)

codegen:compile:memcpy_word_loop
→ dispatch_quick(DISP_CODEGEN_COMPILE, "memcpy_word_loop", NULL)

codegen:bench:memcpy_byte_loop
→ dispatch_quick(DISP_CODEGEN_BENCH, "memcpy_byte_loop", NULL)
```

action 必须是 `search`、`compile`、`bench` 之一。template 是代码模板名称。

### 1.7 task: / result: / tasks 命令

`engine/torkd.c:227-294`

**提交任务**：
```
task:exec:ls -la       → TASK_EXEC
task:analyze:ref.s     → TASK_ANALYZE
task:audit:ref.s:func  → TASK_AUDIT
```

返回 `{"task_id":N,"status":"pending"}` 或 `{"error":"..."}`。

**查询结果**：
```
result:42
→ {"task_id":42,"status":"done","exit_code":0,"output":"..."}
```

状态：`pending` / `running` / `done` / `failed` / `cancelled` / `not_found`。

**队列概览**：
```
tasks
→ {"pending":2,"active":1,"completed":150,"failed":3}
```

### 1.8 soul 命令

`engine/torkd.c:278-288`

```
soul
→ 0x00000001000000000000...
```

返回 Soul 208 字节的 hex dump，前缀 `0x`。Python 侧用 `parse_soul_hex()` 解析。

### 1.9 status 命令

`engine/torkd.c:127-154`

返回多行文本，包含：
- 心跳 tick 数、驱动值
- 硬件压力、世代数
- 活跃分支数、经验条数
- 快照层数、回滚次数
- 能量模式、节流百分比
- TLN 四维输出 (act/mod/exp/nrg)
- 自构建摘要

### 1.10 每 tick 处理

`engine/torkd.c:305-323` (`torkd_tick`)

```
while (accept() 成功):
    设置 2s 超时
    handle_client(client_fd)
```

非阻塞 accept 循环，处理完所有待处理连接后返回 EAGAIN。

### 1.11 C 侧查询接口

`engine/torkd.c:337-364` (`torkd_query`)

引擎内部可以通过同一 socket 自查：
```c
int torkd_query(const char *question, char *response, int max_len);
```

### 1.12 运行检测

`engine/torkd.c:366-379` (`torkd_is_running`)

检查 socket 文件是否存在且可连接，用于外部脚本判断引擎是否在运行。

---

## 2. Web 仪表盘

### 2.1 功能述求

人类需要一个可视化界面来观察 TORK 的实时状态、编辑代码、与 AI 对话、触发进化。Web 仪表盘是一个单页应用，通过 WebSocket 实时推送 Soul 数据。

### 2.2 技术栈

| 组件 | 技术 |
|------|------|
| 后端 | aiohttp (Python) |
| 前端 | 单页 HTML + 原生 JS |
| 编辑器 | CodeMirror 6 (ESM CDN) |
| 实时通信 | WebSocket |
| 语法高亮 | @codemirror/lang-cpp |

### 2.3 后端路由

`web/tork_web.py:397-413` (`create_app`)

| 方法 | 路径 | 处理函数 | 说明 |
|------|------|----------|------|
| GET | `/` | `index` | 返回 dashboard.html |
| GET | `/ws` | `ws_handler` | WebSocket 连接 |
| POST | `/api/exec` | `api_exec` | 执行命令 |
| POST | `/api/evolve` | `api_evolve` | 触发一次进化 |
| GET | `/api/evolution-log` | `api_evolution_log` | 进化日志 |
| GET | `/api/config` | `api_config_get` | 读取 API 配置 |
| POST | `/api/config` | `api_config_set` | 写入 API 配置 |
| GET | `/api/file/{path}` | `api_file_read` | 读取项目文件 |
| POST | `/api/file` | `api_file_write` | 写入项目文件 |
| GET | `/api/inbox` | `api_inbox` | 读取 inbox.md |
| GET | `/api/dir/{path}` | `api_dir` | 列出目录内容 |

### 2.4 WebSocket 协议

`web/tork_web.py:266-305`

**客户端 → 服务端**：

| type | data | 说明 |
|------|------|------|
| `chat` | `{message, history?}` | AI 对话 |
| `exec` | `{command}` | 执行命令 |
| `evolve` | `{}` | 触发进化 |

**服务端 → 客户端**：

| type | data | 说明 |
|------|------|------|
| `update` | `{soul, instincts, engine_running, pid, evolution}` | 实时状态推送 (每秒) |
| `chat_reply` | `{role, content}` | AI 回复 |
| `exec_result` | `{output}` | 命令执行结果 |
| `evolution_result` | `{output, code}` | 进化结果 |
| `error` | `{message}` | 错误 |

### 2.5 后台轮询

`web/tork_web.py:345-391` (`poll_loop`)

每 1 秒执行一次：
1. `pgrep -x tork_core` 或 `pgrep -x tork_engine` 查找 PID
2. `read_soul_from_proc(pid)` 读取 Soul
3. 失败则 `torkd_query("soul")` 通过 socket 获取
4. `_derive_instincts(soul)` 推导三驱力
5. `_evolution_stats()` 读取进化日志
6. 组装 JSON 推送到所有 WebSocket 客户端

### 2.6 本能推导

`web/tork_web.py:84-92` (`_derive_instincts`)

Web 侧的简化推导（非 C 引擎的精确计算）：

```
fear     = min(100, max(0, hw_stress * 30 + (10 if mode==2 else 0)))
desire   = min(100, max(0, 70 - hw_stress * 15 + (10 if mode==1 else 0)))
curiosity = min(100, max(0, 55 - hw_stress * 8 + (15 if mode==0 else 0)))
```

### 2.7 前端标签页

`web/static/dashboard.html`

| 标签 | 内容 |
|------|------|
| Dashboard | 三驱力环形图 + 生命体征条 + Drive 波形 + 状态字段 |
| Editor | 文件树 + CodeMirror 6 编辑器 + 状态面板 |
| Chat | AI 对话界面（通过云端 API 或 torkd） |
| Evolution | 进化统计 + 触发按钮 + 日志 |
| Settings | API 配置 + 引擎启停 |

### 2.8 情绪引擎

`web/static/dashboard.html:848-933`

TORK 的 mode 驱动整个 UI 的视觉氛围：

| Mode | 名称 | 背景色 | 强调色 | 氛围 |
|------|------|--------|--------|------|
| 0 | explore | `#0d0d0d` | `#2196F3` (蓝) | 冷静探索 |
| 1 | seek | `#0d0b0d` | `#d4a017` (琥珀) | 温暖追寻 |
| 2 | cautious | `#0d0808` | `#e04040` (红) | 警觉防御 |

Mode 变化时，背景色、强调色、光晕效果全部切换。对话气泡、心情短语、波形颜色跟随变化。

### 2.9 文件安全

`web/tork_web.py:43-47` (`_safe_path`)

所有文件读写 API 都经过路径安全检查：
```
full = normpath(join(BASE, path))
if full != normpath(BASE) and not full.startswith(SAFE_BASE):
    return None  → 403 Forbidden
```

防止路径遍历攻击。

### 2.10 启动

```bash
python3 web/tork_web.py --port 8420
# 或
./tork.sh dashboard
```

默认绑定 `127.0.0.1:8420`，启动 1 秒后自动打开浏览器。

---

## 3. torkd_bridge 异步桥接

### 3.1 功能述求

Web 仪表盘是异步的（aiohttp），但 torkd socket 是同步阻塞的。桥接层把同步 socket 调用包装为异步，使 Web 处理器不阻塞事件循环。

### 3.2 实现

`web/torkd_bridge.py`

```
ThreadPoolExecutor(max_workers=4)   → 4 个线程执行同步 socket 操作
asyncio.Semaphore(4)               → 最多 4 个并发查询
```

**同步查询** (`_sync_query`)：
1. 创建 AF_UNIX SOCK_STREAM socket
2. 连接 `/tmp/torkd.sock`
3. 发送 `cmd\n`
4. 循环 recv 直到 EOF
5. 返回解码后的字符串

**异步接口** (`torkd_query`)：
```python
async def torkd_query(cmd, timeout=10.0) -> str | None:
    async with _TORKD_SEMAPHORE:
        return await loop.run_in_executor(_TORKD_POOL, _sync_query, cmd, timeout)
```

### 3.3 错误处理

| 错误 | 返回 | 日志级别 |
|------|------|----------|
| ConnectionRefusedError | `None` | WARNING |
| socket.timeout | `None` | WARNING |
| FileNotFoundError | `None` | ERROR |
| 其他异常 | `None` | EXCEPTION |

---

## 4. 云端 API 适配层

### 4.1 功能述求

TORK 需要外部 AI 提供进化方向建议。云端 API 适配层封装了与讯飞星辰 MaaS 的通信，提供有状态对话和无状态单次查询两种模式。

### 4.2 配置

`api/tork_api.py:4-19`

```python
config_path = "api/api_config.json"
# 或环境变量 DEEPSEEK_API_KEY

base_url = "https://maas-coding-api.cn-huabei-1.xf-yun.com/v2"
model    = "astron-code-latest"
temperature = 0.7
max_tokens  = 4096
timeout     = 60
```

### 4.3 有状态对话

`api/tork_api.py:33-62` (`ask`)

```python
conversation.append({"role": "user", "content": message})
payload = {
    "model": model,
    "messages": [system_prompt] + conversation[-10:],  # 最近 10 轮
    "temperature": temperature,
    "max_tokens": max_tokens,
}
reply = POST /v2/chat/completions
conversation.append({"role": "assistant", "content": reply})
return reply
```

System prompt 定义 TORK 为"云端导师"，指导代码进化和运行状态分析。

### 4.4 无状态查询

`api/tork_api.py:64-93` (`ask_simple`)

```python
payload = {
    "model": model,
    "messages": [
        {"role": "system", "content": "你是 TORK 的云端进化导师。你直接输出工程指令。"},
        {"role": "user", "content": message}
    ],
    "temperature": temperature,
}
return POST /v2/chat/completions
```

不保存对话历史，适合单次工程指令。

### 4.5 对话持久化

```python
api.save()   → 保存到 api/conversation.json
api.load()   → 从 api/conversation.json 恢复
```

### 4.6 连接测试

`api/tork_api.py:110-115` (`test_connection`)

```bash
python3 api/tork_api.py
# 输出: 🔌 TORK Cloud Brain Connected. Model: ...
```

---

## 5. 进化引擎

### 5.1 功能述求

进化引擎是 TORK 的"有性繁殖"层——它修改自身源代码，编译验证，成功则提交。与 C 引擎的"无性变异"（TLN 随机变异 + 快照自愈）互补，Python 进化引擎做的是有目的的代码修改。

### 5.2 架构

`cloud/evolution.py`

```
规则引擎 (10 种变异策略)
    +
DeepSeek API (战略方向建议)
    +
适应度反馈 (变异存活时间记录)
```

### 5.3 变异策略

`cloud/evolution.py:135-157` (`_pick_mutation_strategy`)

10 种硬编码策略，按 `gen % 10` 轮转选择：

| 索引 | 目标文件 | 描述 | mutagen | 方法 |
|------|----------|------|---------|------|
| 0 | instinct/instinct.c | cloud collaboration awareness | instinct_cloud_curiosity | 注入 curiosity += 0.12*cw |
| 1 | engine/tork_engine.c | soul_read latency tracking | engine_latency_track | 注入 gettimeofday 代码 |
| 2 | sandbox/sandbox.c | extend dev tools whitelist | sandbox_devtools | 扩展白名单命令 |
| 3 | engine/tork_engine.c | round counter self-awareness | engine_round_tracker | 注入 total_rounds++ |
| 4 | instinct/instinct.c | generation-aware curiosity | instinct_gen_awareness | 注入 curiosity += 0.08*cw |
| 5 | instinct/instinct.c | curiosity_weight +15 | curiosity | 修改结构体字段 |
| 6 | instinct/instinct.c | fear_weight +15 | fear | 修改结构体字段 |
| 7 | instinct/instinct.c | desire_weight +15 | desire | 修改结构体字段 |
| 8 | instinct/instinct.c | conservative_cycle -5 | cycle | 修改结构体字段 |
| 9 | instinct/instinct.c | conservative_cycle +5 | cycle | 修改结构体字段 |

**适应度优先**：如果 `fitness.best_mutagen` 存在，且当前世代不是 5 的倍数，优先使用最佳 mutagen。

### 5.4 变异流程

`cloud/evolution.py:294-371` (`apply_mutation`)

```
1. 备份目标文件 → filepath.evo_bak
2. 执行变异函数 (strategy.apply)
   ├─ 成功 → 继续
   └─ 失败 → 恢复备份，记录 "apply_failed"
3. 编译验证: make -C BASE all (30s 超时)
   ├─ returncode == 0 → 成功
   └─ returncode != 0 → 恢复备份，记录 "compile_failed"
4. 删除备份
5. 更新历史: generation++, successes++
6. 保存进化日志 + 适应度日志
7. git add + commit (可选，失败不阻止)
```

### 5.5 DeepSeek 战略指导

`cloud/evolution.py:113-133` (`_ask_deepseek_for_direction`)

每轮进化前，向 API 发送当前状态摘要：
- 世代数、代码行数/文件数
- 变异总数和成功率
- 最近 5 次变异记录

API 返回一条具体的进化方向建议。当前实现中建议仅打印，不影响策略选择。

### 5.6 自我评估

`cloud/evolution.py:56-111` (`assess_self`)

扫描 22 个核心源文件，统计行数，读取变异引导器 (`persist/mutation_guide.bin`) 的尝试/成功计数，生成评估报告。

### 5.7 运行模式

```bash
# 单次进化
python3 cloud/evolution.py --rounds 1

# 持续进化 (每 600 秒一轮)
python3 cloud/evolution.py --loop --interval 600

# Web 触发
POST /api/evolve  →  _run_evolution()  →  subprocess(evolution.py --once)
```

### 5.8 持久化

| 文件 | 内容 |
|------|------|
| `persist/evolution.json` | `{generation, mutations[], successes, failures}` |
| `persist/fitness.json` | `{history[], best_mutagen, best_score}` |

---

## 6. Fission 分裂机制

### 6.1 功能述求

Fission 是 TORK 的"细胞分裂"——fork 一个子进程，复制项目结构，让子进程独立运行 500 轮，然后竞争：子进程表现更好则父进程让位退出，否则杀掉子进程。

### 6.2 分裂决策

`engine/fission.c:19-39` (`fission_decide`)

```
条件（全部满足才分裂）:
  tick >= 5000                (系统已稳定运行)
  hw_stress < 2               (压力不大)
  drive >= 20                 (有积极驱力)
  无锁文件                     (没有其他分裂在进行)
  π-seed 随机通过 (> 0.4)     (60% 概率通过，40% 延迟)
```

π-seed 随机退避防止"雷群分裂"——多个 tick 同时满足条件时不会同时分裂。

### 6.3 分裂执行

`engine/fission.c:79-128` (`fission_spawn`)

```
1. 创建锁文件 /tmp/tork_fission.lock (O_EXCL 防止并发)
2. fork()
3. 子进程:
   a. prctl(PR_SET_NAME, "tork_child")  → 改名
   b. mkdir("tork_fission_<pid>")
   c. 复制目录: build, core, engine, instinct, code, benchmark
   d. 复制文件: Makefile, build.sh
   e. 复制 benchmark/memcpy 子目录
   f. chdir("tork_fission_<pid>")
   g. execl("./build/tork_engine", "tork_child", "500", NULL)
4. 父进程: usleep(500ms) 等待子进程启动
5. 返回 child_pid
```

子进程运行 500 轮后退出。复制使用 fork+execl(cp) 而非 system()，避免 shell 注入。

### 6.4 竞争与收割

`engine/fission.c:130-156` (`fission_collect`) + `engine/fission.c:158-180` (`fission_migrate`)

```
fission_collect(child_pid, timeout_ticks=20):
  1. 等待 20 * 50ms = 1 秒
  2. soul_open(&child_soul, child_pid)  → 读取子进程 Soul
  3. child_score = child_opt_saved + (child_nop_count == 0 ? 1 : 0)
  4. 返回 child_score > 1 ? 0 (子胜) : 1 (父胜)

fission_migrate(child_pid):
  result = fission_collect(child_pid, 20)
  if result == 0:
      printf("child wins, parent yielding")
      fission_cleanup()
      exit(0)            ← 父进程退出，子进程接管
  else:
      kill(child_pid, SIGTERM)
      waitpid(child_pid)
      spawn_rm_rf("tork_fission_<pid>")  ← 清理子进程目录
      fission_cleanup()
```

竞争标准很简单：子进程的代码优化数 + NOP 清零加分 > 1 则子进程胜出。

### 6.5 锁机制

```
创建: open("/tmp/tork_fission.lock", O_WRONLY|O_CREAT|O_EXCL, 0644)
写入: 当前 PID
清理: unlink("/tmp/tork_fission.lock")
```

O_EXCL 确保只有一个分裂进程能创建锁文件。

### 6.6 生命周期

```
tick_fission (每 1000 tick)
  → fission_decide()          检查条件
  → tork_dispatch(FISSION)    记录到经验
  → fission_spawn()           fork 子进程
  → fission_migrate()         竞争 + 收割
```

---

## 7. Grid 共享内存

### 7.1 功能述求

Grid 是 TORK 的终端可视化——一个 80×40 的字符网格，用 ANSI 24-bit 颜色渲染实时状态。引擎每 tick 把 Soul 数据推送到共享内存，Grid 进程读取并渲染。

### 7.2 共享内存结构

`grid/grid_soul_connector.c:22-26`

```
soul_grid_shm_t:
  write_count    uint64     写入计数（同步用）
  soul           grid_soul_feed_t  Soul 数据副本
```

大小：`sizeof(grid_soul_feed_t) + 8`

### 7.3 grid_soul_feed_t

`grid/tork_grid.h:25-47`

```
grid_soul_feed_t:
  tick              uint32     心跳计数
  drive             int8       驱动值
  hw_stress         uint8      硬件压力
  gen_count         uint32     世代数
  active_branches   int        活跃分支数
  experience_count  uint32     经验条数
  peer_count        int        分布式节点数
  energy_mode       uint8      能量模式
  fear              uint8      恐惧 (0-100)
  desire            uint8      欲望 (0-100)
  curiosity         uint8      好奇 (0-100)
  branch_drive[8]   int8       各分支驱动
  branch_ticks[8]   uint32     各分支 tick 数
  recent_outcomes[16] int8     最近经验结果
  outcome_count     uint8      结果数量
```

### 7.4 引擎侧写入

`grid/grid_soul_connector.c:32-62`

```
grid_engine_init():
  1. open("/tmp/tork_soul_grid.bin", O_CREAT|O_RDWR)  ← 优先文件
  2. 失败则 shm_open("/dev/shm/tork_soul.bin", ...)   ← 共享内存
  3. ftruncate → mmap → MAP_SHARED

grid_engine_write(soul):
  write_count++
  memcpy(&shm->soul, soul, sizeof)
```

`scheduler_tick` 中的 `grid_soul_feed()` 每次调用 `grid_engine_write()`。

### 7.5 Grid 侧读取

`grid/grid_soul_connector.c:78-113`

```
grid_viewer_init():
  open("/tmp/tork_soul_grid.bin", O_RDONLY)  ← 优先文件
  失败则 shm_open(...)  ← 共享内存
  mmap → MAP_SHARED (只读)

grid_viewer_read(out):
  memcpy(out, &shm->soul, sizeof)
```

### 7.6 渲染布局

`grid/tork_grid.c:341-397` (`grid_render`)

80×40 网格分区：

| 行 | 内容 | 说明 |
|----|------|------|
| 0 | 状态栏 | `♥<tick> D<drive> S<stress> G<gen> \| B<branch> E<exp> P<peer> M<energy>` |
| 1-8 | 心跳波形 | drive + stress 映射为高度，8 行高，滚动 |
| 9 | 空行 | 分隔 |
| 10-15 | 本能条 | fear (红) / desire (绿) / curiosity (蓝)，3 列 |
| 16 | 空行 | 分隔 |
| 17-22 | 分支健康 | 最多 8 个分支，每个 10 列宽，颜色随 drive 变化 |
| 23 | 空行 | 分隔 |
| 24-33 | 经验热力图 | 16 个最近经验结果，正值红/绿，负值蓝 |
| 34 | 空行 | 分隔 |
| 35-39 | 底部状态 | `TRK ♥<global_tick> \| M<mode> \| ALIVE/PAUSED` |

### 7.7 像素模型

`grid/tork_grid.h:49-58`

每个像素有独立的 RGB、亮度、好奇心、衰减率、影响力。非可视化区域的像素做"涌现"计算——邻居亮度求和 + 随机波动 + 衰减，形成有机的背景动画。

### 7.8 清理

```
grid_engine_cleanup():
  munmap → close → shm_unlink → remove("/tmp/tork_soul_grid.bin")

grid_viewer_cleanup():
  munmap → close
```

---

## 8. 数据流总览

```
                    ┌─────────────────────────────────────────┐
                    │         tork_engine (C)                  │
                    │  scheduler_tick() 每 500ms              │
                    └──┬──────┬──────┬──────┬──────────────────┘
                       │      │      │      │
              ┌────────▼┐  ┌──▼───┐ ┌▼─────┐ ┌▼──────────────┐
              │ torkd   │  │ grid │ │ dist │ │ persist/      │
              │ .sock   │  │ shm  │ │ net  │ │ *.bin         │
              └────┬────┘  └──┬───┘ └──────┘ └───────────────┘
                   │          │
         ┌─────────▼──┐    ┌──▼──────────┐
         │ torkd_     │    │ tork_grid   │
         │ bridge.py  │    │ (终端渲染)  │
         └─────┬──────┘    └─────────────┘
               │
    ┌──────────▼──────────────────────────────┐
    │         tork_web.py (aiohttp)            │
    │  REST API + WebSocket + CodeMirror 6    │
    │  ← poll_loop: 每秒读 Soul + 推送 WS     │
    └──┬──────┬──────┬────────────────────────┘
       │      │      │
  ┌────▼──┐ ┌▼────┐ ┌▼──────────┐
  │ 浏览器 │ │ API │ │ evolution │
  │  UI   │ │适配层│ │ .py      │
  └───────┘ └──┬──┘ └───────────┘
               │
        ┌──────▼──────────┐
        │ 讯飞星辰 MaaS    │
        │ /v2/chat/       │
        │ completions     │
        └─────────────────┘

    Fission 路径:
    tork_engine → fork() → tork_child (500 轮)
                              ↓
                         fission_migrate()
                         ├─ 子胜 → 父 exit(0)
                         └─ 父胜 → kill(child)
```

---

## 验证清单

- [ ] `echo "ping" | socat - UNIX-CONNECT:/tmp/torkd.sock` 返回 `pong`
- [ ] `echo "status" | socat - UNIX-CONNECT:/tmp/torkd.sock` 返回多行状态
- [ ] `echo "soul" | socat - UNIX-CONNECT:/tmp/torkd.sock` 返回 `0x` 开头的 hex
- [ ] `echo "exec:echo hello" | socat - UNIX-CONNECT:/tmp/torkd.sock` 返回 `hello`
- [ ] `echo "task:exec:ls" | socat - UNIX-CONNECT:/tmp/torkd.sock` 返回 `{"task_id":...}`
- [ ] `python3 web/tork_web.py --port 8420 --no-open` 启动后 `curl http://localhost:8420/api/config` 返回 JSON
- [ ] 浏览器打开 `http://localhost:8420` 看到 Dashboard 标签页
- [ ] `python3 api/tork_api.py` 测试云端连接（需要有效 API key）
- [ ] `python3 cloud/evolution.py --rounds 1` 执行一次进化（需要 make 可用）
- [ ] `ls /tmp/tork_soul_grid.bin` 引擎运行时存在 Grid 共享内存文件
- [ ] `./build/tork_grid` 运行 Grid 终端可视化（需要引擎在运行）