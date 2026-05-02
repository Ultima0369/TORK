# TORK — The Organism That Reads and Knows

A bare-metal heartbeat system with instinct-driven code self-modification.

## Architecture

```
tork_project/
├── core/                  ── Bare-metal heartbeat core
│   ├── tork_core.asm      ── x86-64 asm, no libc, mmap Soul @ 0x200000
│   └── tork_soul.inc      ── Soul layout offsets (single source of truth)
│
├── engine/                ── C control layer
│   ├── tork_engine.c      ── Main loop: soul_read → instinct → drive → action
│   ├── soul_access.h      ── /proc/PID/mem bridge (read + ptrace write)
│   └── monitor.c/h        ── Sensor reading (/proc status, temperature)
│
├── instinct/              ── Instinct layer
│   ├── instinct.h         ── fear/desire/curiosity types
│   └── instinct.c         ── Instinct evaluation from soul state
│
├── code/                  ── Code operation layer
│   ├── code_reader.h/c    ── Read asm, count insns, classify opcodes
│   └── code_modifier.h/c  ── Replace opcodes, delete dead code, verify
│
├── benchmark/             ── Test targets
│   └── memcpy/
│       ├── ref.c          ── Simple memcpy source
│       └── ref.s          ── Generated assembly (TORK reads and modifies this)
│
├── build/                 ── Build output (.o files and executables)
├── Makefile               ── Build entry
├── build.sh               ── Build script (fallback)
└── README.md
```

## Soul Layout (64 bytes used, 96 allocated)

```
Offset  Field              Type      Description
0x00    S_TICK             uint32    Heartbeat counter
0x04    S_LAST_TSC         uint64    Previous TSC value
0x0C    S_CUR_TSC          uint64    Current TSC value
0x14    S_ELAPSED          uint64    TSC delta (cur - last)
0x1C    S_EXPECTED         uint64    Drive-adjusted expected interval
0x24    S_HW_STRESS        uint8     Hardware stress level (0-3)
0x25    S_MODE             uint8     Operating mode
0x26    S_PAD              uint8[2]  Alignment padding
0x28    S_CRC              uint32    CRC32 checksum (drive excluded)
0x2C    S_SELF_PID         uint32    Core process PID
0x30    S_DRIVE            int8      Instinct drive (-128..+127)
0x31    S_RESERVED2        uint8     Reserved
0x32    S_PPID             uint16    Parent PID
0x34    S_CODE_INSNS       uint16    Instruction count
0x36    S_CODE_MOV         uint16    MOV-class count
0x38    S_CODE_ARITH       uint16    Arithmetic-class count
0x3A    S_CODE_CTRL        uint16    Control-flow count
0x3C    S_CODE_OTHER       uint16    Other instruction count
0x3E    S_CODE_MOD_SUCCESS uint8     0=none, 1=success, 2=failed
0x3F    S_CODE_OPT_SAVED   uint8     Dead code lines deleted
```

## Build

```bash
make clean && make all    # Full build, zero warnings
make run                  # Run with 10 rounds
./build/tork_engine 100   # Run with 100 rounds
```

Requires: `gcc`, `as` (GNU assembler), `ld`, root/cap_sys_rawio for MSR temperature.

## How It Works

1. **Core** (asm): mmap Soul at 0x200000, heartbeat loop with RDTSCP, temperature sensing, CRC32 self-verify
2. **Engine** (C): fork+exec core, read Soul via /proc/PID/mem, evaluate instinct, write drive back
3. **Instinct**: fear (stress), desire (stability), curiosity (safety) → drive value
4. **Drive**: adjusts heartbeat speed (positive=fast, negative=slow), excluded from CRC
5. **Code reading**: every 200 rounds, parse ref.s, count/classify instructions
6. **Code modification**: every 300 rounds, conservative je→jz replacement with verification
7. **Code optimization**: every 600 rounds, delete dead code (insns after ret) with verification

## Evolution

| Egg | Capability |
|-----|-----------|
| 1 | Heartbeat core + 96-byte Soul + CRC32 |
| 2 | Temperature sensing (MSR/sysfs) + hw_stress |
| 3 | C bridge via /proc/PID/mem |
| 4 | Instinct layer (fear/desire/curiosity) |
| 5 | Drive write-back, closed loop |
| 6 | Process awareness (pid/ppid) |
| 7 | Code reading (instruction count/classification) |
| 8 | Conservative code modification (je→jz) |
| 9 | Dead code elimination |
| 10 | Project skeleton (modular build system) |
