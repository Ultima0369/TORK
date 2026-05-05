#ifndef SOUL_ACCESS_H
#define SOUL_ACCESS_H

#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#define SOUL_ADDR_VAL  0x200000
#define SOUL_SIZE      192
#define SOUL_PAGE      4096

/* Soul field offsets — must match tork_soul.inc exactly (v3.1) */
#define S_TICK        0x00  /* uint32 */
#define S_LAST_TSC    0x04  /* uint64 */
#define S_CUR_TSC     0x0C  /* uint64 */
#define S_ELAPSED     0x14  /* uint64 */
#define S_EXPECTED    0x1C  /* uint64 */
#define S_HW_STRESS   0x24  /* uint8  */
#define S_MODE        0x25  /* uint8  */
#define S_PAD         0x26  /* uint8[2] */
#define S_CRC         0x28  /* uint32 */
#define S_SELF_PID    0x2C  /* uint32 */
#define S_DRIVE       0x30  /* int8   */
#define S_RESERVED2   0x31  /* uint8  */
#define S_PPID        0x32  /* uint16 */
#define S_CODE_INSNS  0x34  /* uint16 */
#define S_CODE_MOV    0x36  /* uint16 */
#define S_CODE_ARITH  0x38  /* uint16 */
#define S_CODE_CTRL   0x3A  /* uint16 */
#define S_CODE_OTHER  0x3C  /* uint16 */
#define S_CODE_MOD_SUCCESS 0x3E  /* uint8 */
#define S_CODE_OPT_SAVED  0x3F  /* uint8 */
#define S_CODE_NOP_COUNT 0x40  /* uint8 */
#define S_FISSION_COUNT 0x41  /* uint8 */
#define S_CHILD_PID     0x42  /* uint16 */
#define S_FISSION_TICK  0x44  /* uint16 */
#define S_WINS          0x46  /* uint16 */

/* v2.0 新字段 */
#define S_AGREED            0x48  /* uint8 */
#define S_SANDBOX_LEVEL     0x49  /* uint8 */
#define S_CLOUD_CONNECTED   0x4A  /* uint8 */
#define S_CLOUD_PROVIDER    0x4B  /* uint8 */
#define S_LEARN_COUNT       0x4C  /* uint16 */
#define S_MUTATION_COUNT    0x4E  /* uint16 */
#define S_BEST_SCORE        0x50  /* uint32 */
#define S_GEN_COUNT         0x54  /* uint32 */
#define S_HEARTBEAT_MS      0x58  /* uint16 — 心跳间隔(ms)，默认100，大脑可改写 */

/* v3.0 学习字段 */
#define S_EXPERIENCE_COUNT  0x60  /* uint32 */
#define S_EXPERIENCE_SAVED  0x64  /* uint32 */
#define S_LEARNING_RATE     0x68  /* uint16 */
#define S_CURIOSITY_DECAY   0x6A  /* uint16 */
#define S_MCTS_ITERATIONS   0x6C  /* uint16 */
#define S_LAST_IDLE_TICK    0x6E  /* uint32 */
#define S_BEST_OUTCOME      0x72  /* int16  */
#define S_WORST_OUTCOME     0x74  /* int16  */
#define S_RESERVED4         0x76  /* uint8[10] */

/* v3.15 TLN hint 字段 (借用 RESERVED4 前4字节) */
#define S_TLN_ACTION       0x76  /* int8  : +1激进, -1保守, 0悬置 */
#define S_TLN_MODIFY       0x77  /* int8  : +1可变异, -1禁变异, 0悬置 */
#define S_TLN_EXPLORE      0x78  /* int8  : +1探索, -1收敛, 0悬置 */
#define S_TLN_ENERGY       0x79  /* int8  : +1高功率, -1省电, 0悬置 */


/* v3.1 分岔字段 */
#define S_BRANCH_ID         0x80  /* uint32 */
#define S_PARENT_ID         0x84  /* uint32 */
#define S_BRANCH_GEN        0x88  /* uint32 */
#define S_MAX_TICKS         0x8C  /* uint32 */
#define S_DEATH_REPORT      0x90  /* uint64 */
#define S_BRANCH_SOUL_PTR   0x98  /* uint64 */
#define S_BRANCH_TICKS      0xA0  /* uint32 */
#define S_BRANCH_DRIVE_PEAK 0xA4  /* int16 */
#define S_BRANCH_DRIVE_END  0xA6  /* int16 */

/* ── Soul reader/writer via /proc/PID/mem ───────────────────── */
typedef struct {
    int    mem_fd;    /* fd for /proc/PID/mem (O_RDONLY) */
    int    wr_fd;     /* fd for /proc/PID/mem (O_RDWR) — needs ptrace */
    pid_t  pid;
    uint8_t buf[SOUL_SIZE];
} soul_t;

/* Open /proc/pid/mem for reading and writing */
static inline int soul_open(soul_t *s, pid_t pid) {
    s->pid = pid;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/mem", pid);
    s->mem_fd = open(path, O_RDONLY);
    s->wr_fd = -1;
    if (s->mem_fd < 0) return -1;
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) == 0) {
        waitpid(pid, NULL, 0);
        int wfd = open(path, O_RDWR);
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        if (wfd >= 0) s->wr_fd = wfd;
    }
    return 0;
}

/* Snapshot the full 96-byte soul into internal buffer */
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

static inline int soul_write_byte(soul_t *s, uint32_t offset, uint8_t val) {
    if (s->wr_fd < 0) return -1;
    if (ptrace(PTRACE_ATTACH, s->pid, NULL, NULL) != 0) return -1;
    waitpid(s->pid, NULL, 0);
    if (lseek(s->wr_fd, SOUL_ADDR_VAL + offset, SEEK_SET) == (off_t)-1) {
        ptrace(PTRACE_DETACH, s->pid, NULL, NULL);
        return -1;
    }
    ssize_t w = write(s->wr_fd, &val, 1);
    ptrace(PTRACE_DETACH, s->pid, NULL, NULL);
    return (w == 1) ? 0 : -1;
}

static inline int soul_write_buf(soul_t *s, uint32_t offset, const void *data, size_t len) {
    if (s->wr_fd < 0) return -1;
    if (ptrace(PTRACE_ATTACH, s->pid, NULL, NULL) != 0) return -1;
    waitpid(s->pid, NULL, 0);
    if (lseek(s->wr_fd, SOUL_ADDR_VAL + offset, SEEK_SET) == (off_t)-1) {
        ptrace(PTRACE_DETACH, s->pid, NULL, NULL);
        return -1;
    }
    ssize_t w = write(s->wr_fd, data, len);
    ptrace(PTRACE_DETACH, s->pid, NULL, NULL);
    return (w == (ssize_t)len) ? 0 : -1;
}

static inline int soul_set_drive(soul_t *s, int8_t drive) {
    return soul_write_byte(s, S_DRIVE, (uint8_t)drive);
}

/* Accessor macros */
#define SOUL_U32(s, off)  __extension__({ uint32_t _v; memcpy(&_v, (s)->buf + (off), 4); _v; })
#define SOUL_U64(s, off)  __extension__({ uint64_t _v; memcpy(&_v, (s)->buf + (off), 8); _v; })
#define SOUL_U32_SET(s, off, val) do { uint32_t _v = (val); memcpy((s)->buf + (off), &_v, 4); } while(0)
#define SOUL_U64_SET(s, off, val) do { uint64_t _v = (val); memcpy((s)->buf + (off), &_v, 8); } while(0)
#define SOUL_U16(s, off)  (*(uint16_t*)((s)->buf + (off)))
#define SOUL_U8(s, off)   ((s)->buf[(off)])

static inline uint32_t  soul_tick(soul_t *s)              { return SOUL_U32(s, S_TICK); }
static inline uint64_t  soul_last_tsc(soul_t *s)          { return SOUL_U64(s, S_LAST_TSC); }
static inline uint64_t  soul_cur_tsc(soul_t *s)           { return SOUL_U64(s, S_CUR_TSC); }
static inline uint64_t  soul_elapsed(soul_t *s)           { return SOUL_U64(s, S_ELAPSED); }
static inline uint64_t  soul_expected(soul_t *s)           { return SOUL_U64(s, S_EXPECTED); }
static inline uint8_t   soul_hw_stress(soul_t *s)         { return SOUL_U8(s, S_HW_STRESS); }
static inline uint8_t   soul_mode(soul_t *s)               { return SOUL_U8(s, S_MODE); }
static inline uint32_t  soul_checksum(soul_t *s)           { return SOUL_U32(s, S_CRC); }
static inline uint32_t  soul_self_pid(soul_t *s)           { return SOUL_U32(s, S_SELF_PID); }
static inline int8_t    soul_drive(soul_t *s)              { return (int8_t)SOUL_U8(s, S_DRIVE); }
static inline uint16_t  soul_ppid(soul_t *s)               { return SOUL_U16(s, S_PPID); }
static inline uint16_t  soul_code_insns(soul_t *s)         { return SOUL_U16(s, S_CODE_INSNS); }
static inline uint16_t  soul_code_mov(soul_t *s)           { return SOUL_U16(s, S_CODE_MOV); }
static inline uint16_t  soul_code_arith(soul_t *s)         { return SOUL_U16(s, S_CODE_ARITH); }
static inline uint16_t  soul_code_ctrl(soul_t *s)          { return SOUL_U16(s, S_CODE_CTRL); }
static inline uint16_t  soul_code_other(soul_t *s)         { return SOUL_U16(s, S_CODE_OTHER); }
static inline uint8_t   soul_code_mod_success(soul_t *s)   { return SOUL_U8(s, S_CODE_MOD_SUCCESS); }
static inline uint8_t   soul_code_opt_saved(soul_t *s)     { return SOUL_U8(s, S_CODE_OPT_SAVED); }
static inline uint8_t   soul_code_nop_count(soul_t *s)     { return SOUL_U8(s, S_CODE_NOP_COUNT); }
static inline uint8_t   soul_fission_count(soul_t *s)      { return SOUL_U8(s, S_FISSION_COUNT); }
static inline uint16_t  soul_child_pid(soul_t *s)          { return SOUL_U16(s, S_CHILD_PID); }
static inline uint16_t  soul_fission_tick(soul_t *s)       { return SOUL_U16(s, S_FISSION_TICK); }
static inline uint16_t  soul_wins(soul_t *s)               { return SOUL_U16(s, S_WINS); }

/* v2.0 新字段访问器 */
static inline uint8_t   soul_agreed(soul_t *s)             { return SOUL_U8(s, S_AGREED); }
static inline uint8_t   soul_sandbox_level(soul_t *s)      { return SOUL_U8(s, S_SANDBOX_LEVEL); }
static inline uint8_t   soul_cloud_connected(soul_t *s)    { return SOUL_U8(s, S_CLOUD_CONNECTED); }
static inline uint8_t   soul_cloud_provider(soul_t *s)     { return SOUL_U8(s, S_CLOUD_PROVIDER); }
static inline uint16_t  soul_learn_count(soul_t *s)        { return SOUL_U16(s, S_LEARN_COUNT); }
static inline uint16_t  soul_mutation_count(soul_t *s)     { return SOUL_U16(s, S_MUTATION_COUNT); }
static inline uint32_t  soul_best_score(soul_t *s)         { return SOUL_U32(s, S_BEST_SCORE); }
static inline uint32_t  soul_gen_count(soul_t *s)          { return SOUL_U32(s, S_GEN_COUNT); }

/* 心跳间隔：大脑改写此值控制ASM心跳速度 */
static inline uint16_t  soul_heartbeat_ms(soul_t *s)      { return SOUL_U16(s, S_HEARTBEAT_MS); }
static inline int soul_set_heartbeat_ms(soul_t *s, uint16_t ms) {
    if (ms < 10) ms = 10;     /* 最低10ms，防止烧CPU */
    if (ms > 5000) ms = 5000; /* 最高5秒，防止假死 */
    return soul_write_buf(s, S_HEARTBEAT_MS, &ms, 2);
}

/* v3.0 访问器 */
static inline uint32_t  soul_experience_count(soul_t *s) { return SOUL_U32(s, S_EXPERIENCE_COUNT); }
static inline uint32_t  soul_experience_saved(soul_t *s) { return SOUL_U32(s, S_EXPERIENCE_SAVED); }
static inline uint16_t  soul_learning_rate(soul_t *s)    { return SOUL_U16(s, S_LEARNING_RATE); }
static inline uint16_t  soul_curiosity_decay(soul_t *s)  { return SOUL_U16(s, S_CURIOSITY_DECAY); }
static inline uint16_t  soul_mcts_iterations(soul_t *s)  { return SOUL_U16(s, S_MCTS_ITERATIONS); }
static inline uint32_t  soul_last_idle_tick(soul_t *s)   { return SOUL_U32(s, S_LAST_IDLE_TICK); }
static inline int16_t   soul_best_outcome(soul_t *s)     { return (int16_t)SOUL_U16(s, S_BEST_OUTCOME); }
static inline int16_t   soul_worst_outcome(soul_t *s)    { return (int16_t)SOUL_U16(s, S_WORST_OUTCOME); }

/* v3.15 TLN 访问器 */
static inline int8_t    soul_tln_action(soul_t *s)      { return (int8_t)SOUL_U8(s, S_TLN_ACTION); }
static inline int8_t    soul_tln_modify(soul_t *s)      { return (int8_t)SOUL_U8(s, S_TLN_MODIFY); }
static inline int8_t    soul_tln_explore(soul_t *s)     { return (int8_t)SOUL_U8(s, S_TLN_EXPLORE); }
static inline int8_t    soul_tln_energy(soul_t *s)      { return (int8_t)SOUL_U8(s, S_TLN_ENERGY); }


/* v3.1 分支字段访问器 */
static inline uint32_t  soul_branch_id(soul_t *s)          { return SOUL_U32(s, S_BRANCH_ID); }
static inline uint32_t  soul_parent_id(soul_t *s)         { return SOUL_U32(s, S_PARENT_ID); }
static inline uint32_t  soul_branch_gen(soul_t *s)        { return SOUL_U32(s, S_BRANCH_GEN); }
static inline uint32_t  soul_max_ticks(soul_t *s)         { return SOUL_U32(s, S_MAX_TICKS); }
static inline uint64_t  soul_death_report(soul_t *s)      { return SOUL_U64(s, S_DEATH_REPORT); }
static inline uint64_t  soul_branch_soul_ptr(soul_t *s)   { return SOUL_U64(s, S_BRANCH_SOUL_PTR); }
static inline uint32_t  soul_branch_ticks(soul_t *s)      { return SOUL_U32(s, S_BRANCH_TICKS); }
static inline int16_t   soul_branch_drive_peak(soul_t *s) { return (int16_t)SOUL_U16(s, S_BRANCH_DRIVE_PEAK); }
static inline int16_t   soul_branch_drive_end(soul_t *s)  { return (int16_t)SOUL_U16(s, S_BRANCH_DRIVE_END); }

/* CRC32 verification of the snapshot */
int soul_verify(soul_t *s);

#endif /* SOUL_ACCESS_H */
