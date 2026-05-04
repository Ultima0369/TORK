# TORK Soul Layout v3.0 — 扩展学习/经验字段
# Single source of truth for Soul field offsets.
# tork_core.asm (.include) and soul_access.h (#define) must match.

.equ S_TICK,              0x00            # uint32  — Heartbeat counter
.equ S_LAST_TSC,          0x04            # uint64  — Previous TSC value
.equ S_CUR_TSC,           0x0C            # uint64  — Current TSC value
.equ S_ELAPSED,           0x14            # uint64  — TSC delta (cur - last)
.equ S_EXPECTED,          0x1C            # uint64  — Drive-adjusted expected interval
.equ S_HW_STRESS,         0x24            # uint8   — Hardware stress level (0-3)
.equ S_MODE,              0x25            # uint8   — Operating mode
.equ S_PAD,               0x26            # uint8[2] — Alignment padding
.equ S_CRC,               0x28            # uint32  — CRC32 checksum (drive excluded)
.equ S_SELF_PID,          0x2C            # uint32  — written once by C layer
.equ S_DRIVE,             0x30            # int8    — from C instinct layer (-128..+127)
.equ S_RESERVED2,         0x31            # uint8
.equ S_PPID,              0x32            # uint16  — Parent PID
.equ S_CODE_INSNS,        0x34            # uint16  — Instruction count
.equ S_CODE_MOV,          0x36            # uint16  — MOV-class count
.equ S_CODE_ARITH,        0x38            # uint16  — Arithmetic-class count
.equ S_CODE_CTRL,         0x3A            # uint16  — Control-flow count
.equ S_CODE_OTHER,        0x3C            # uint16  — Other instruction count
.equ S_CODE_MOD_SUCCESS,  0x3E            # uint8: 0=none,1=ok,2=fail
.equ S_CODE_OPT_SAVED,    0x3F            # uint8: cumulative dead code lines deleted
.equ S_CODE_NOP_COUNT,    0x40            # uint8: nop insns found in last scan
.equ S_FISSION_COUNT,     0x41            # uint8: fission count (max 255)
.equ S_CHILD_PID,         0x42            # uint16: child instance PID (0=none)
.equ S_FISSION_TICK,      0x44            # uint16: tick at last fission (low bits)
.equ S_WINS,              0x46            # uint16: sovereignty migration wins

# ── v2.0 新增字段 ──────────────────────────────────────────────
.equ S_AGREED,            0x48            # uint8: 0=unknown, 1=agreed, 2=rejected
.equ S_SANDBOX_LEVEL,     0x49            # uint8: 0=none 1=read 2=safe 3=normal 4=full
.equ S_CLOUD_CONNECTED,   0x4A            # uint8: 0=disconnected, 1=connected
.equ S_CLOUD_PROVIDER,    0x4B            # uint8: 0=none, 1=DeepSeek, 2=Claude, 3=GPT
.equ S_LEARN_COUNT,       0x4C            # uint16: cloud-guided learning count
.equ S_MUTATION_COUNT,    0x4E            # uint16: self-mutation count
.equ S_BEST_SCORE,        0x50            # uint32: best evolutionary fitness
.equ S_GEN_COUNT,         0x54            # uint32: generation count
.equ S_RESERVED3,         0x58            # uint8[8]: future expansion

# ── v3.0 学习字段 (Soul 总大小扩展至 192 字节) ────────────────
.equ S_EXPERIENCE_COUNT,  0x60            # uint32 — total experiences recorded
.equ S_EXPERIENCE_SAVED,  0x64            # uint32 — last saved experience count
.equ S_LEARNING_RATE,     0x68            # uint16 — learning rate (fixed point, 0-1000)
.equ S_CURIOSITY_DECAY,   0x6A            # uint16 — curiosity decay per idle cycle (0-1000)
.equ S_MCTS_ITERATIONS,   0x6C            # uint16 — MCTS iterations in last idle cycle
.equ S_LAST_IDLE_TICK,    0x6E            # uint32 — tick when last idle occurred
.equ S_BEST_OUTCOME,      0x72            # int16  — best outcome ever achieved
.equ S_WORST_OUTCOME,     0x74            # int16  — worst outcome ever
.equ S_RESERVED4,         0x76            # uint8[10]: future expansion

# Soul total size = 0x80 = 128 bytes (扩展至 192 字节，更紧凑)
# 预留: 0x80 - 0xC0 未来扩展
