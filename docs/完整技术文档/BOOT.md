# BOOT.md — TORK 启动与装载

> 5 秒摘要：`make all` 编译出两个二进制 → `tork_engine` fork 出 `tork_core` → ptrace attach → 通过 /proc/PID/mem 读写 Soul → 初始化 20+ 子系统 → 进入主循环。

---

## 1. 编译：make all 产出了什么

```
make all
```

产出 7 个二进制，全部放在 `build/` 目录：

| 二进制 | 源码 | 作用 |
|--------|------|------|
| `tork_core` | `core/tork_core.asm` | ASM 心跳进程，独立运行，mmap Soul @ 0x200000 |
| `tork_engine` | `engine/tork_engine.c` + 34 个 .o | C 主引擎，fork tork_core 后通过 /proc/PID/mem 读写 Soul |
| `tork_sandbox` | `sandbox/sandbox_cli.c` | 独立沙箱 CLI 工具 |
| `tork_ask` | `engine/tork_ask.c` | 独立问答 CLI |
| `torkd_start` | `engine/torkd_start.c` | 独立守护进程（无 Soul，仅 socket 服务） |
| `tork` | `engine/tork_cli.c` | CLI 客户端 |
| `tork_grid` | `grid/grid_main.c` | 网格计算节点 |

关键依赖链：

```
tork_core.asm  →  as  →  tork_core.o  →  ld  →  tork_core
tork_engine.c  →  gcc  →  tork_engine.o
  + 34 个模块 .o  →  gcc -o tork_engine ... -lm
```

`tork_core` 是纯汇编，无 libc 依赖，直接 syscall。`tork_engine` 链接了 34 个 .o 文件和 libm。

---

## 2. 启动：./tork.sh start 发生了什么

```
./tork.sh start
```

实际执行的是 `python3 floating/tork_daemon.py all`，它会启动引擎和仪表盘。

如果直接运行 `./tork.sh engine`，则执行 `./build/tork_engine`。

---

## 3. tork_engine 的启动流程

入口：`engine/tork_engine.c:192` (`main`)

### 3.1 解析命令行参数

```
./build/tork_engine [N]         # 运行 N 轮后退出
./build/tork_engine --daemon    # 无限运行（默认）
./build/tork_engine --restore   # 从磁盘恢复状态
./build/tork_engine --fresh     # 不恢复，全新启动
./build/tork_engine --quiet     # 减少输出
```

### 3.2 fork tork_core

```
tork_engine.c:65  start_core()
```

1. `fork()` 创建子进程
2. 子进程：关闭 stdout → `execl("build/tork_core", "tork_core", NULL)`
3. 父进程：`usleep(200000)` 等待 200ms 让 core 启动
4. 保存 `core_pid`

此时系统中有两个进程：
- **tork_core**（子）：ASM 心跳循环，mmap Soul @ 0x200000，每 100ms 一次 tick
- **tork_engine**（父）：C 引擎，通过 /proc/PID/mem 读写 Soul

### 3.3 注册信号处理

```
tork_engine.c:215-216
signal(SIGINT, cleanup_core);
signal(SIGTERM, cleanup_core);
```

`cleanup_core` 在退出时按顺序保存所有子系统状态（见第 8 节）。

### 3.4 打开 Soul

```
tork_engine.c:218-223  soul_open(&soul, core_pid)
```

这是整个系统的关键连接点。`soul_open` 做了三件事：

1. 打开 `/proc/{core_pid}/mem`（只读 fd → `mem_fd`）
2. 尝试 `ptrace(PTRACE_ATTACH, core_pid)` → 成功后打开 `/proc/{core_pid}/mem`（读写 fd → `wr_fd`）→ `ptrace(PTRACE_DETACH)`
3. 初始化内部 208 字节缓冲区 `soul.buf`

之后所有对 Soul 的读取通过 `soul_read()`（lseek + read），写入通过 `soul_write_byte/buf()`（ptrace attach → lseek → write → ptrace detach）。

### 3.5 初始化子系统

```
tork_engine.c:79-107  init_subsystems()
```

按顺序初始化 20+ 个模块：

```
bb_init()          黑板（共享内存 0x300000，跨进程通信）
exp_init()         经验环形缓冲（4096 条，persist/experience.bin）
br_init()          分支管理器
pat_init()         模式学习器（64 槽位）
tune_init()        自调参引擎（初始 fear=1.0, desire=0.7, curiosity=1.15）
pat_load()         从磁盘加载已有模式
pidx_init()        π 索引（节奏指纹匹配）
pidx_load()
obs_init()         观察者（异常检测基线）
obs_load_baseline()
snap_init()        快照引擎（自愈回滚）
snap_load()
eng_init()         能量管理器
self_cal_init()    自校准器
watcher_init()     进程监控器
watcher_load()
sb_init()          自构建检测器（源文件变更监控）
sb_load()
mg_init()          变异引导器
mg_load()
pi_seed_init()     π 随机种子（基于 TSC 的确定性随机源）
dispatch_init()    统一调度层
codegen_init()     代码生成器（MCTS 搜索汇编变体）
task_init()        异步任务队列
ind_init()         归纳推理器（共享内存 0x302000）
```

注意：`ind_init()` 失败只打 warning，不阻止启动。

### 3.6 磁盘恢复（如果 --restore）

```
tork_engine.c:110-127  do_restore_state()
```

1. `ps_restore_all()` 恢复黑板、参数、归纳规则到共享内存
2. `ps_restore_soul()` 恢复 Soul 的 208 字节，提取 tick 值写回 Soul

恢复的文件来自 `persist/` 目录：

```
persist/soul.bin          Soul 快照
persist/blackboard.bin    黑板数据
persist/params.bin       本能参数
persist/rules.bin        归纳规则
persist/manifest.json    校验清单
```

### 3.7 启动服务

```
tork_engine.c:129-143  init_services()
```

三个服务，全部非致命（失败只打 warning）：

| 服务 | 路径/地址 | 作用 |
|------|-----------|------|
| torkd | `/tmp/torkd.sock` | Unix Socket 服务，接受外部命令 |
| distributed | 网络端口 | 分布式节点通信 |
| grid | `/dev/shm/tork_soul.bin` | 共享内存，供网格节点读取 Soul |

### 3.8 初始化 Soul 字段

```
tork_engine.c:146-189  init_soul_fields()
```

写入以下字段：

| 字段 | 来源 |
|------|------|
| `S_SELF_PID` (0x2C) | `core_pid`（tork_core 的 PID） |
| `S_PPID` (0x32) | 从 `/proc/{core_pid}/status` 读 `PPid` |
| `S_AGREED` (0x48) | 从 `/etc/tork/agreement.sig` 读许可协议状态 |
| `S_SANDBOX_LEVEL` (0x49) | 同上文件 |
| `S_LEARNING_RATE` (0x68) | 默认 500 |
| `S_CURIOSITY_DECAY` (0x6A) | 默认 100 |
| `S_EXPERIENCE_COUNT` (0x60) | 当前经验条数 |

---

## 4. 主循环

```
tork_engine.c:240-313
```

```
for (i = 0; ; i++) {
    1. soul_read()          — 快照 Soul 到本地缓冲
    2. 构建 instinct_input  — 从 Soul 字段组装本能输入
    3. instinct_evaluate()  — 计算三驱力 (fear/desire/curiosity)
    4. 计算 drive           — (desire - fear + curiosity) * 100
    5. pat_query_best_action — 查询模式库推荐行动
    6. scheduler_tick()     — 所有周期任务的唯一入口
    7. usleep(heartbeat_interval * 1000)  — 默认 500ms
}
```

`soul_read()` 连续失败 10 次以上会报警，失败 10+ 次后每 100 次打 warning。如果 core 进程死亡，循环退出。

---

## 5. 关机流程

```
tork_engine.c:316-329  +  cleanup_core()  (信号处理)
```

退出时按顺序执行：

```
kill(core_pid, SIGTERM)     → 停止 ASM 心跳
waitpid(core_pid)           → 等待 core 退出
ps_save_all(soul.buf)       → 保存 Soul/黑板/参数/规则到磁盘
ps_cleanup_baks()           → 清理 .bak 文件
fission_cleanup()           → 清理 fission 锁文件
bb_cleanup()                → 释放黑板共享内存
self_cal_save()             → 保存自校准数据
ind_cleanup()               → 释放归纳规则共享内存
soul_close()                → 关闭 /proc/PID/mem fd
```

信号处理版本 (`cleanup_core`) 额外保存：
```
torkd_shutdown()            → 关闭 Unix Socket
dist_cleanup()              → 清理分布式
grid_engine_cleanup()       → 清理共享内存
ps_emergency_save()         → 紧急保存 Soul
snap_save()                 → 保存快照
watcher_save()              → 保存监控数据
sb_save()                   → 保存自构建数据
mg_save()                   → 保存变异引导
obs_save_baseline()          → 保存观察者基线
pat_save()                  → 保存模式库
pidx_save()                 → 保存 π 索引
task_cleanup()              → 清理任务队列
br_cleanup()                → 清理分支
exp_save()                  → 保存经验缓冲
```

---

## 6. 数据流总览

```
                    ┌─────────────────────────────────────┐
                    │           tork_core (ASM)            │
                    │  mmap Soul @ 0x200000                │
                    │  每 100ms: tick++ → TOR → 温度感知    │
                    └──────────────┬──────────────────────┘
                                   │ /proc/PID/mem
                                   │ (ptrace 读写)
                    ┌──────────────▼──────────────────────┐
                    │         tork_engine (C)              │
                    │  soul_read() → 本地 208B 缓冲       │
                    │  instinct_evaluate() → drive         │
                    │  scheduler_tick() → 周期任务          │
                    │  soul_write_*() → 写回 Soul          │
                    └──┬──────┬──────┬──────┬──────────────┘
                       │      │      │      │
              ┌────────▼┐  ┌──▼───┐ ┌▼────┐ ┌▼──────────┐
              │ torkd   │  │ grid │ │ dist│ │ persist/  │
              │ .sock   │  │ shm  │ │ net │ │ *.bin     │
              └─────────┘  └──────┘ └─────┘ └───────────┘
```

---

## 7. 独立守护进程模式

`./build/torkd_start` 可以不依赖 tork_core 独立运行：

```
fork() → setsid() → 关闭 stdio → 初始化子系统 → torkd_init(NULL) → 循环 torkd_tick()
```

此模式下没有 Soul，socket 命令中 `status` 和 `soul` 不可用，但 `ping`、`exec:`、`audit:` 等仍可工作。

---

## 验证清单

- [x] `make all` 成功编译，`build/` 下有 7 个二进制
- [x] `./build/tork_engine 3` 运行 3 轮，输出包含 "TORK engine started"
- [x] 运行期间 `pgrep -a tork_core` 能看到 ASM 心跳进程
- [x] `cat /proc/$(pgrep tork_core)/maps | grep 200000` 确认 Soul 映射（0x200000 rw-p 匿名）
- [x] `ldd build/tork_core` 确认无 libc（"not a dynamic executable"）
- [ ] `./tork.sh status` 显示 engine running
- [ ] `ls persist/` 能看到 soul.bin, manifest.json 等文件
- [ ] `./build/tork_engine --restore 5` 能从磁盘恢复并输出 "restored from disk"

### 实际验证记录（2026-05-05）

**tork_core 内存映射确认：**
```
00200000-00201000 rw-p 00000000 00:00 0    [anon]
```
- 地址 0x200000，Soul 共享内存
- 权限 rw-p（读写，私有匿名映射）
- tork_core 通过 mmap(MAP_FIXED|MAP_ANONYMOUS|MAP_PRIVATE) 分配

**tork_core 无 libc 确认：**
```
not a dynamic executable
```
- 纯 ASM + syscall，无动态链接依赖，最小攻击面

**tork_engine 启动序列确认：**
```
EXP: loaded 1030 experiences
PAT: loaded 15 patterns (cycle=117)
TUNE: loaded params (curiosity=2.15)
PIDX: loaded 2 profiles
OBS: loaded baseline
SNAP: loaded 8 snapshots
WATCH: loaded 413 events
SELF: monitoring 64 source files
MGUIDE: 8 strategies registered
TORKD: socket ready at /tmp/torkd.sock
DIST: distributed blackboard ready
GRID: soul shared memory ready at /dev/shm/tork_soul.bin
TORK agreement: ACCEPTED (sandbox level 3)
TORK engine started. core PID=9821
```

**关键发现：**

1. **TORK 是 OS 后用户态启动** — 不参与 CMOS/Bootloader/内核启动，是普通用户进程
2. **tork_core 是 tork_engine 的子进程** — fork+exec，非独立启动
3. **Soul 通过 /proc/PID/mem 共享** — 不是共享内存 IPC，是 ptrace+procfs 读写
4. **沙箱是自研命令级过滤** — 不是 OS 级隔离，是 TORK 自己写的白名单+黑名单
5. **TORK 只从宿主获取硬件实情** — 温度、TSC、进程状态，不直接改动宿主基座
6. **安全回滚靠快照自愈** — 只回滚 Soul 状态，不回滚文件系统
7. **未来路径** — 技术成熟后解除沙箱，TORK 心跳可住进 CMOS/CPU 指令集本身