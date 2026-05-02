# TORK Soul Layout v0.9
# This file is the single source of truth for Soul field offsets.
# Both tork_core.asm (.include) and soul_access.h (#define) must match.
# When modifying: recalculate offsets manually and update BOTH files.

.equ S_TICK,              0x00            # uint32
.equ S_LAST_TSC,          0x04            # uint64
.equ S_CUR_TSC,           0x0C            # uint64
.equ S_ELAPSED,           0x14            # uint64
.equ S_EXPECTED,          0x1C            # uint64
.equ S_HW_STRESS,         0x24            # uint8
.equ S_MODE,              0x25            # uint8
.equ S_PAD,               0x26            # uint8[2]
.equ S_CRC,               0x28            # uint32
.equ S_SELF_PID,          0x2C            # uint32 — written once by C layer
.equ S_DRIVE,             0x30            # int8 — from C instinct layer
.equ S_RESERVED2,         0x31            # uint8
.equ S_PPID,              0x32            # uint16
.equ S_CODE_INSNS,        0x34            # uint16
.equ S_CODE_MOV,          0x36            # uint16
.equ S_CODE_ARITH,        0x38            # uint16
.equ S_CODE_CTRL,         0x3A            # uint16
.equ S_CODE_OTHER,        0x3C            # uint16
.equ S_CODE_MOD_SUCCESS,  0x3E            # uint8: 0=none,1=ok,2=fail
.equ S_CODE_OPT_SAVED,    0x3F            # uint8: cumulative dead code lines deleted
.equ S_CODE_NOP_COUNT,    0x40            # uint8: nop insns found in last scan
.equ S_FISSION_COUNT,     0x41            # uint8: fission count (max 255)
.equ S_CHILD_PID,         0x42            # uint16: child instance PID (0=none)
.equ S_FISSION_TICK,      0x44            # uint16: tick at last fission (low bits)
.equ S_WINS,              0x46            # uint16: sovereignty migration wins
