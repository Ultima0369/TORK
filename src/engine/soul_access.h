#ifndef SOUL_ACCESS_H
#define SOUL_ACCESS_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#define SOUL_ADDR_VAL  0x200000
#define SOUL_SIZE      208
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
#define S_RESERVED3         0x58  /* uint8[6] */
#define S_HEARTBEAT_MS      0x5E  /* uint16 — 心跳间隔(ms)，默认100，大脑可改写 */

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

/* v3.17 P0 接口约束: 32字节预留 node_id + consensus_vector */
#define S_NODE_ID            0xA8  /* uint8[16] — 节点唯一标识 */
#define S_CONSENSUS_VECTOR   0xB8  /* uint8[16] — 共识向量 */

/* ── Soul reader/writer via /proc/PID/mem ───────────────────── */
typedef struct {
    int    mem_fd;    /* fd for /proc/PID/mem (O_RDONLY) */
    int    wr_fd;     /* fd for /proc/PID/mem (O_RDWR) — needs ptrace */
    pid_t  pid;
    uint8_t buf[SOUL_SIZE];
    /* 铁律 Section 2: ptrace 门控状态 */
    uint8_t audit_token[16];  /* attach 审计令牌 (RDRAND+TSC) */
    int     audit_passed;     /* 审计是否通过 */
    uint8_t pre_snapshot[SOUL_SIZE]; /* detach 前快照，用于回滚 */
} soul_t;

/* ── Inline implementations (macros, accessors, ptrace protocol) ── */
#include "soul_access_impl.h"

/* ── Non-inline function declarations ──────────────────────── */

/* CRC32 verification of the snapshot */
int soul_verify(soul_t *s);

/* CRC32 computation and update */
uint32_t soul_compute_crc(const soul_t *s);
void soul_update_crc(soul_t *s);
int soul_verify_crc(const soul_t *s);

/* Golden backup and restore */
int soul_save_golden(soul_t *s, pid_t core_pid);
int soul_restore_golden(soul_t *s, pid_t core_pid);

#endif /* SOUL_ACCESS_H */
