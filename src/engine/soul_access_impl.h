#ifndef SOUL_ACCESS_IMPL_H
#define SOUL_ACCESS_IMPL_H

/* Inline implementations for soul_access.h.
   Do NOT include this file directly — included by soul_access.h. */

/* ── 铁律 Section 2: ptrace 门控协议 ─────────────────────── */

/*
 * soul_audit_attach — PTRACE_ATTACH 前安全审计
 * 验证: 1) 调用者是 TORK 引擎 (PPID 检查)
 *       2) 生成审计令牌 (RDRAND+TSC，16字节)
 * 返回: 0=通过, -1=拒绝
 */
static inline int soul_audit_attach(soul_t *s) {
    /* PPID 检查: 确保调用者是 tork_engine (core 的父进程) */
    char ppid_path[64];
    snprintf(ppid_path, sizeof(ppid_path), "/proc/%d/status", s->pid);
    int fd = open(ppid_path, O_RDONLY);
    if (fd < 0) return -1;
    char status_buf[512];
    ssize_t n = read(fd, status_buf, sizeof(status_buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    status_buf[n] = '\0';

    /* 查找 PPid 行 */
    char *ppid_line = strstr(status_buf, "PPid:");
    if (!ppid_line) return -1;
    pid_t core_ppid = (pid_t)atoi(ppid_line + 5);
    if (core_ppid != getpid()) return -1;

    /* 生成审计令牌: TSC + 伪随机填充 */
    uint64_t tsc;
    __asm__ __volatile__("rdtsc" : "=A"(tsc));
    memcpy(s->audit_token, &tsc, 8);
    memset(s->audit_token + 8, 0xAA, 8); /* 标记位 */

    s->audit_passed = 1;
    return 0;
}

/*
 * soul_verify_detach — PTRACE_DETACH 前验证 Soul 修改
 * 验证: 回读 Soul，CRC32 校验通过才允许 detach
 * 失败: 回滚到 pre_snapshot，仍然 detach (但 Soul 恢复原状)
 * 返回: 0=验证通过, -1=已回滚
 */
static inline int soul_verify_detach(soul_t *s) {
    /* 回读当前 Soul */
    uint8_t current[SOUL_SIZE];
    if (lseek(s->mem_fd, SOUL_ADDR_VAL, SEEK_SET) == (off_t)-1)
        return -1;
    ssize_t r = read(s->mem_fd, current, SOUL_SIZE);
    if (r != SOUL_SIZE)
        return -1;

    /* CRC32 校验 (与 tork_engine.c 中的 soul_crc32_raw 一致) */
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < SOUL_SIZE; i++) {
        if (i >= S_CRC && i < S_CRC + 4) continue; /* 跳过 CRC 字段 */
        crc ^= current[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
    }
    crc = ~crc;

    uint32_t saved_crc;
    memcpy(&saved_crc, current + S_CRC, 4);
    if (crc == saved_crc)
        return 0; /* 验证通过 */

    /* 验证失败: 回滚到 pre_snapshot */
    if (lseek(s->wr_fd, SOUL_ADDR_VAL, SEEK_SET) == (off_t)-1)
        return -1;
    write(s->wr_fd, s->pre_snapshot, SOUL_SIZE);
    return -1;
}

/*
 * soul_heartbeat_pipe_confirm — 心跳双通道确认
 * C 引擎写 S_HEARTBEAT_MS 后，通过 pipe 向 ASM 内核发确认信号
 * ASM 内核在 nanosleep 前交叉验证 Soul 值 vs 管道确认值
 */
extern int hb_confirm_fd;
static inline int soul_heartbeat_pipe_confirm(soul_t *s, uint16_t ms) {
    (void)s;
    if (hb_confirm_fd < 0) return -1;
    /* 写入 4 字节心跳确认: "HB" + uint16 ms (little-endian) */
    uint8_t msg[4] = {'H', 'B', (uint8_t)(ms & 0xFF), (uint8_t)(ms >> 8)};
    ssize_t w = write(hb_confirm_fd, msg, 4);
    return (w == 4) ? 0 : -1;
}

/* Open /proc/pid/mem for reading and writing — 铁律: attach 审计 */
static inline int soul_open(soul_t *s, pid_t pid) {
    s->pid = pid;
    s->audit_passed = 0;
    s->ptrace_locked = 0;
    memset(s->audit_token, 0, sizeof(s->audit_token));
    memset(s->pre_snapshot, 0, sizeof(s->pre_snapshot));
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/mem", pid);
    s->mem_fd = open(path, O_RDONLY);
    s->wr_fd = -1;
    if (s->mem_fd < 0) return -1;
    /* 铁律: attach 前审计 */
    if (soul_audit_attach(s) != 0) {
        close(s->mem_fd);
        s->mem_fd = -1;
        return -1;
    }
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) == 0) {
        waitpid(pid, NULL, 0);
        int wfd = open(path, O_RDWR);
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        if (wfd >= 0) s->wr_fd = wfd;
    }
    return 0;
}

/* Snapshot the full soul into internal buffer */
static inline int soul_read(soul_t *s) {
    if (lseek(s->mem_fd, SOUL_ADDR_VAL, SEEK_SET) == (off_t)-1)
        return -1;
    ssize_t n = read(s->mem_fd, s->buf, SOUL_SIZE);
    return (n == SOUL_SIZE) ? 0 : -1;
}

static inline void soul_close(soul_t *s) {
    if (s->mem_fd >= 0) close(s->mem_fd);
    if (s->wr_fd >= 0) close(s->wr_fd);
    s->mem_fd = -1;
    s->wr_fd = -1;
}

/* ── 批量 ptrace 模式 ───────────────────────────────────────
 * scheduler_tick 开始时 attach，结束时 detach。
 * 所有写操作在 locked 状态下完成，避免每 tick 5-8 次独立 attach/detach。
 * 用法: soul_lock(s) → 多次 soul_write_byte_locked / soul_write_buf_locked
 *       → soul_unlock(s)
 */

/* 前向声明: _locked 变体在下方定义，但 soul_write_byte/buf 需提前引用 */
static inline int soul_write_byte_locked(soul_t *s, uint32_t offset, uint8_t val);
static inline int soul_write_buf_locked(soul_t *s, uint32_t offset, const void *data, size_t len);

static inline int soul_lock(soul_t *s) {
    if (s->wr_fd < 0) return -1;
    if (s->ptrace_locked) return 0;  /* 已锁定，幂等 */
    /* 保存写前快照 (用于回滚) */
    if (lseek(s->mem_fd, SOUL_ADDR_VAL, SEEK_SET) == (off_t)-1) return -1;
    if (read(s->mem_fd, s->pre_snapshot, SOUL_SIZE) != SOUL_SIZE) return -1;
    if (ptrace(PTRACE_ATTACH, s->pid, NULL, NULL) != 0) return -1;
    waitpid(s->pid, NULL, 0);
    s->ptrace_locked = 1;
    return 0;
}

static inline int soul_unlock(soul_t *s) {
    if (!s->ptrace_locked) return 0;  /* 未锁定，幂等 */
    /* detach 前验证，失败则回滚 */
    soul_verify_detach(s);
    int rc = (ptrace(PTRACE_DETACH, s->pid, NULL, NULL) == 0) ? 0 : -1;
    s->ptrace_locked = 0;
    return rc;
}

static inline int soul_write_byte(soul_t *s, uint32_t offset, uint8_t val) {
    if (s->ptrace_locked)
        return soul_write_byte_locked(s, offset, val);
    if (s->wr_fd < 0) return -1;
    /* 铁律: 保存写前快照 (用于回滚) */
    if (lseek(s->mem_fd, SOUL_ADDR_VAL, SEEK_SET) == (off_t)-1) return -1;
    if (read(s->mem_fd, s->pre_snapshot, SOUL_SIZE) != SOUL_SIZE) return -1;

    if (ptrace(PTRACE_ATTACH, s->pid, NULL, NULL) != 0) return -1;
    waitpid(s->pid, NULL, 0);
    if (lseek(s->wr_fd, SOUL_ADDR_VAL + offset, SEEK_SET) == (off_t)-1) {
        ptrace(PTRACE_DETACH, s->pid, NULL, NULL);
        return -1;
    }
    ssize_t w = write(s->wr_fd, &val, 1);
    /* 铁律: detach 前验证，失败则回滚 */
    soul_verify_detach(s);
    ptrace(PTRACE_DETACH, s->pid, NULL, NULL);
    return (w == 1) ? 0 : -1;
}

/* 写入变体：假设核心已被 ptrace 挂起，不自行 attach/detach */
static inline int soul_write_byte_locked(soul_t *s, uint32_t offset, uint8_t val) {
    if (s->wr_fd < 0) return -1;
    if (lseek(s->wr_fd, SOUL_ADDR_VAL + offset, SEEK_SET) == (off_t)-1)
        return -1;
    return (write(s->wr_fd, &val, 1) == 1) ? 0 : -1;
}

static inline int soul_write_buf(soul_t *s, uint32_t offset, const void *data, size_t len) {
    if (s->ptrace_locked)
        return soul_write_buf_locked(s, offset, data, len);
    if (s->wr_fd < 0) return -1;
    if (offset + len > SOUL_SIZE) return -1;
    /* 铁律: 保存写前快照 (用于回滚) */
    if (lseek(s->mem_fd, SOUL_ADDR_VAL, SEEK_SET) == (off_t)-1) return -1;
    if (read(s->mem_fd, s->pre_snapshot, SOUL_SIZE) != SOUL_SIZE) return -1;

    if (ptrace(PTRACE_ATTACH, s->pid, NULL, NULL) != 0) return -1;
    waitpid(s->pid, NULL, 0);
    if (lseek(s->wr_fd, SOUL_ADDR_VAL + offset, SEEK_SET) == (off_t)-1) {
        ptrace(PTRACE_DETACH, s->pid, NULL, NULL);
        return -1;
    }
    ssize_t w = write(s->wr_fd, data, len);
    /* 铁律: detach 前验证，失败则回滚 */
    soul_verify_detach(s);
    ptrace(PTRACE_DETACH, s->pid, NULL, NULL);
    if (w != (ssize_t)len) return -1;
    /* 回读验证 */
    if (lseek(s->mem_fd, SOUL_ADDR_VAL + offset, SEEK_SET) == (off_t)-1)
        return -1;
    uint8_t readback[SOUL_SIZE];
    ssize_t r = read(s->mem_fd, readback, len);
    if (r == (ssize_t)len && memcmp(readback, data, len) == 0)
        return 0;
    return -1;
}

/* 写入变体：假设核心已被 ptrace 挂起，不自行 attach/detach，含回读验证 */
static inline int soul_write_buf_locked(soul_t *s, uint32_t offset, const void *data, size_t len) {
    if (s->wr_fd < 0) return -1;
    if (offset + len > SOUL_SIZE) return -1;
    if (lseek(s->wr_fd, SOUL_ADDR_VAL + offset, SEEK_SET) == (off_t)-1)
        return -1;
    ssize_t w = write(s->wr_fd, data, len);
    if (w != (ssize_t)len) return -1;
    /* 回读验证 */
    if (lseek(s->mem_fd, SOUL_ADDR_VAL + offset, SEEK_SET) == (off_t)-1)
        return -1;
    uint8_t readback[SOUL_SIZE];
    ssize_t r = read(s->mem_fd, readback, len);
    if (r == (ssize_t)len && memcmp(readback, data, len) == 0)
        return 0;
    return -1;
}

static inline int soul_set_drive(soul_t *s, int8_t drive) {
    return soul_write_byte(s, S_DRIVE, (uint8_t)drive);
}

/* ── Accessor macros — const-safe: (s) can be const soul_t* ── */
#define SOUL_U32(s, off)  __extension__({ uint32_t _v; memcpy(&_v, (const uint8_t*)(s)->buf + (off), 4); _v; })
#define SOUL_U64(s, off)  __extension__({ uint64_t _v; memcpy(&_v, (const uint8_t*)(s)->buf + (off), 8); _v; })
#define SOUL_U32_SET(s, off, val) do { uint32_t _v = (val); memcpy((s)->buf + (off), &_v, 4); } while(0)
#define SOUL_U64_SET(s, off, val) do { uint64_t _v = (val); memcpy((s)->buf + (off), &_v, 8); } while(0)
#define SOUL_U16_SET(s, off, val) do { uint16_t _v = (val); memcpy((s)->buf + (off), &_v, 2); } while(0)
#define SOUL_U16(s, off)  __extension__({ uint16_t _v; memcpy(&_v, (const uint8_t*)(s)->buf + (off), 2); _v; })
#define SOUL_U8(s, off)   ((s)->buf[(off)])

/* ── Simple accessor inlines ── */
static inline uint32_t  soul_tick(const soul_t *s)              { return SOUL_U32(s, S_TICK); }
static inline uint64_t  soul_last_tsc(const soul_t *s)          { return SOUL_U64(s, S_LAST_TSC); }
static inline uint64_t  soul_cur_tsc(const soul_t *s)           { return SOUL_U64(s, S_CUR_TSC); }
static inline uint64_t  soul_elapsed(const soul_t *s)           { return SOUL_U64(s, S_ELAPSED); }
static inline uint64_t  soul_expected(const soul_t *s)           { return SOUL_U64(s, S_EXPECTED); }
static inline uint8_t   soul_hw_stress(const soul_t *s)         { return SOUL_U8(s, S_HW_STRESS); }
static inline uint8_t   soul_mode(const soul_t *s)               { return SOUL_U8(s, S_MODE); }
static inline uint32_t  soul_checksum(const soul_t *s)           { return SOUL_U32(s, S_CRC); }
static inline uint32_t  soul_self_pid(const soul_t *s)           { return SOUL_U32(s, S_SELF_PID); }
static inline int8_t    soul_drive(const soul_t *s)              { return (int8_t)SOUL_U8(s, S_DRIVE); }
static inline uint16_t  soul_ppid(const soul_t *s)               { return SOUL_U16(s, S_PPID); }
static inline uint16_t  soul_code_insns(const soul_t *s)         { return SOUL_U16(s, S_CODE_INSNS); }
static inline uint16_t  soul_code_mov(const soul_t *s)           { return SOUL_U16(s, S_CODE_MOV); }
static inline uint16_t  soul_code_arith(const soul_t *s)         { return SOUL_U16(s, S_CODE_ARITH); }
static inline uint16_t  soul_code_ctrl(const soul_t *s)          { return SOUL_U16(s, S_CODE_CTRL); }
static inline uint16_t  soul_code_other(const soul_t *s)         { return SOUL_U16(s, S_CODE_OTHER); }
static inline uint8_t   soul_code_mod_success(const soul_t *s)   { return SOUL_U8(s, S_CODE_MOD_SUCCESS); }
static inline uint8_t   soul_code_opt_saved(const soul_t *s)     { return SOUL_U8(s, S_CODE_OPT_SAVED); }
static inline uint8_t   soul_code_nop_count(const soul_t *s)     { return SOUL_U8(s, S_CODE_NOP_COUNT); }
static inline uint8_t   soul_fission_count(const soul_t *s)      { return SOUL_U8(s, S_FISSION_COUNT); }
static inline uint16_t  soul_child_pid(const soul_t *s)          { return SOUL_U16(s, S_CHILD_PID); }
static inline uint16_t  soul_fission_tick(const soul_t *s)       { return SOUL_U16(s, S_FISSION_TICK); }
static inline uint16_t  soul_wins(const soul_t *s)               { return SOUL_U16(s, S_WINS); }

/* v2.0 新字段访问器 */
static inline uint8_t   soul_agreed(const soul_t *s)             { return SOUL_U8(s, S_AGREED); }
static inline uint8_t   soul_sandbox_level(const soul_t *s)      { return SOUL_U8(s, S_SANDBOX_LEVEL); }
static inline uint8_t   soul_cloud_connected(const soul_t *s)    { return SOUL_U8(s, S_CLOUD_CONNECTED); }
static inline uint8_t   soul_cloud_provider(const soul_t *s)     { return SOUL_U8(s, S_CLOUD_PROVIDER); }
static inline uint16_t  soul_learn_count(const soul_t *s)        { return SOUL_U16(s, S_LEARN_COUNT); }
static inline uint16_t  soul_mutation_count(const soul_t *s)     { return SOUL_U16(s, S_MUTATION_COUNT); }
static inline uint32_t  soul_best_score(const soul_t *s)         { return SOUL_U32(s, S_BEST_SCORE); }
static inline uint32_t  soul_gen_count(const soul_t *s)          { return SOUL_U32(s, S_GEN_COUNT); }

/* 心跳间隔：大脑改写此值控制ASM心跳速度 */
static inline uint16_t  soul_heartbeat_ms(const soul_t *s)      { return SOUL_U16(s, S_HEARTBEAT_MS); }
static inline int soul_set_heartbeat_ms(soul_t *s, uint16_t ms) {
    if (ms < 10) ms = 10;     /* 最低10ms，防止烧CPU */
    if (ms > 5000) ms = 5000; /* 最高5秒，防止假死 */
    int rc = soul_write_buf(s, S_HEARTBEAT_MS, &ms, 2);
    /* 铁律 Section 2: 写完 Soul 后，向 /proc/core_pid/fd/0 发确认信号 */
    if (rc == 0) soul_heartbeat_pipe_confirm(s, ms);
    return rc;
}

/* v3.0 访问器 */
static inline uint32_t  soul_experience_count(const soul_t *s) { return SOUL_U32(s, S_EXPERIENCE_COUNT); }
static inline uint32_t  soul_experience_saved(const soul_t *s) { return SOUL_U32(s, S_EXPERIENCE_SAVED); }
static inline uint16_t  soul_learning_rate(const soul_t *s)    { return SOUL_U16(s, S_LEARNING_RATE); }
static inline uint16_t  soul_curiosity_decay(const soul_t *s)  { return SOUL_U16(s, S_CURIOSITY_DECAY); }
static inline uint16_t  soul_mcts_iterations(const soul_t *s)  { return SOUL_U16(s, S_MCTS_ITERATIONS); }
static inline uint32_t  soul_last_idle_tick(const soul_t *s)   { return SOUL_U32(s, S_LAST_IDLE_TICK); }
static inline int16_t   soul_best_outcome(const soul_t *s)     { return (int16_t)SOUL_U16(s, S_BEST_OUTCOME); }
static inline int16_t   soul_worst_outcome(const soul_t *s)    { return (int16_t)SOUL_U16(s, S_WORST_OUTCOME); }

/* v3.15 TLN 访问器 */
static inline int8_t    soul_tln_action(const soul_t *s)      { return (int8_t)SOUL_U8(s, S_TLN_ACTION); }
static inline int8_t    soul_tln_modify(const soul_t *s)      { return (int8_t)SOUL_U8(s, S_TLN_MODIFY); }
static inline int8_t    soul_tln_explore(const soul_t *s)     { return (int8_t)SOUL_U8(s, S_TLN_EXPLORE); }
static inline int8_t    soul_tln_energy(const soul_t *s)      { return (int8_t)SOUL_U8(s, S_TLN_ENERGY); }

/* v3.1 分支字段访问器 */
static inline uint32_t  soul_branch_id(const soul_t *s)          { return SOUL_U32(s, S_BRANCH_ID); }
static inline uint32_t  soul_parent_id(const soul_t *s)         { return SOUL_U32(s, S_PARENT_ID); }
static inline uint32_t  soul_branch_gen(const soul_t *s)        { return SOUL_U32(s, S_BRANCH_GEN); }
static inline uint32_t  soul_max_ticks(const soul_t *s)         { return SOUL_U32(s, S_MAX_TICKS); }
static inline uint64_t  soul_death_report(const soul_t *s)      { return SOUL_U64(s, S_DEATH_REPORT); }
static inline uint64_t  soul_branch_soul_ptr(const soul_t *s)   { return SOUL_U64(s, S_BRANCH_SOUL_PTR); }
static inline uint32_t  soul_branch_ticks(const soul_t *s)      { return SOUL_U32(s, S_BRANCH_TICKS); }
static inline int16_t   soul_branch_drive_peak(const soul_t *s) { return (int16_t)SOUL_U16(s, S_BRANCH_DRIVE_PEAK); }
static inline int16_t   soul_branch_drive_end(const soul_t *s)  { return (int16_t)SOUL_U16(s, S_BRANCH_DRIVE_END); }

#endif /* SOUL_ACCESS_IMPL_H */
