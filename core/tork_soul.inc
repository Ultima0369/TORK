# TORK Soul Layout v3.0 — 单一起源
# core/tork_core.asm (.include) 和 engine/soul_access.h 必须保持一致

.equ S_TICK,              0x00    # uint32 — Heartbeat counter
.equ S_LAST_TSC,          0x04    # uint64 — Previous TSC value
.equ S_CUR_TSC,           0x0C    # uint64 — Current TSC value
.equ S_ELAPSED,           0x14    # uint64 — TSC delta
.equ S_EXPECTED,          0x1C    # uint64 — Drive-adjusted expected interval
.equ S_HW_STRESS,         0x24    # uint8  — Hardware stress level (0-3)
.equ S_MODE,              0x25    # uint8  — Operating mode / pos_streak proxy
.equ S_PAD,               0x26    # uint8[2]
.equ S_CRC,               0x28    # uint32 — CRC32 checksum
.equ S_SELF_PID,          0x2C    # uint32
.equ S_DRIVE,             0x30    # int8  — from C instinct layer
.equ S_RESERVED2,         0x31    # uint8 — tor_bias proxy
.equ S_PPID,              0x32    # uint16
.equ S_CODE_INSNS,        0x34    # uint16
.equ S_CODE_MOV,          0x36    # uint16
.equ S_CODE_ARITH,        0x38    # uint16
.equ S_CODE_CTRL,         0x3A    # uint16
.equ S_CODE_OTHER,        0x3C    # uint16
.equ S_CODE_MOD_SUCCESS,  0x3E    # uint8
.equ S_CODE_OPT_SAVED,    0x3F    # uint8
.equ S_CODE_NOP_COUNT,    0x40    # uint8
.equ S_FISSION_COUNT,     0x41    # uint8
.equ S_CHILD_PID,         0x42    # uint16
.equ S_FISSION_TICK,      0x44    # uint16
.equ S_WINS,              0x46    # uint16

# v2.0
.equ S_AGREED,            0x48    # uint8
.equ S_SANDBOX_LEVEL,     0x49    # uint8
.equ S_CLOUD_CONNECTED,   0x4A    # uint8
.equ S_CLOUD_PROVIDER,    0x4B    # uint8
.equ S_LEARN_COUNT,       0x4C    # uint16
.equ S_MUTATION_COUNT,    0x4E    # uint16
.equ S_BEST_SCORE,        0x50    # uint32
.equ S_GEN_COUNT,         0x54    # uint32
.equ S_RESERVED3,         0x58    # uint8[8]

# v3.0 学习字段
.equ S_EXPERIENCE_COUNT,  0x60    # uint32
.equ S_EXPERIENCE_SAVED,  0x64    # uint32
.equ S_LEARNING_RATE,     0x68    # uint16
.equ S_CURIOSITY_DECAY,   0x6A    # uint16
.equ S_MCTS_ITERATIONS,   0x6C    # uint16
.equ S_LAST_IDLE_TICK,    0x6E    # uint32
.equ S_BEST_OUTCOME,      0x72    # int16
.equ S_WORST_OUTCOME,     0x74    # int16
.equ S_RESERVED4,         0x76    # uint8[10]

# v3.1 分岔字段
.equ S_BRANCH_ID,         0x80    # uint32
.equ S_PARENT_ID,         0x84    # uint32
.equ S_BRANCH_GEN,        0x88    # uint32
.equ S_MAX_TICKS,         0x8C    # uint32
.equ S_DEATH_REPORT,      0x90    # uint64
.equ S_BRANCH_SOUL_PTR,   0x98    # uint64
.equ S_BRANCH_TICKS,      0xA0    # uint32
.equ S_BRANCH_DRIVE_PEAK, 0xA4    # int16
.equ S_BRANCH_DRIVE_END,  0xA6    # int16
.equ S_RESERVED5,         0xA8    # uint8[24]

# Soul 总大小
.equ SOUL_SIZE_BYTES,     192
