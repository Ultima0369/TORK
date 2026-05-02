# TORK Core v0.2 - Heartbeat + Temperature Sense
# x86-64 Linux userspace, no libc
# Assemble: as -o tork_core.o tork_core.asm
# Link:     ld -o tork_core tork_core.o

# ── Constants ──────────────────────────────────────────────────────
.equ SOUL_ADDR,      0x200000
.equ SOUL_SIZE,      96
.equ PAGE_SIZE,      4096
.equ EXPECTED_TSC,   300000000

# Soul layout — single source of truth in tork_soul.inc
.include "tork_soul.inc"

# syscalls
.equ SYS_READ,       0
.equ SYS_WRITE,      1
.equ SYS_OPEN,       2
.equ SYS_CLOSE,      3
.equ SYS_LSEEK,      8
.equ SYS_MMAP,       9
.equ SYS_EXIT,       60

# open flags
.equ O_RDONLY,       0
.equ SEEK_SET,       0

# mmap flags
.equ PROT_RW,        0x03
.equ MAP_FPA,        0x32             # MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS

# temperature thresholds
.equ TEMP_70,        70
.equ TEMP_80,        80
.equ TEMP_85,        85
.equ TEMP_50,        50

# cooling period
.equ FAIL_LIMIT,     10
.equ COOLDOWN_TICKS, 300

# ── Read-only Data ────────────────────────────────────────────────
.section .rodata
msr_path:     .ascii "/dev/cpu/0/msr\0"
tz0_path:     .ascii "/sys/class/thermal/thermal_zone0/temp\0"
tz1_path:     .ascii "/sys/class/thermal/thermal_zone1/temp\0"
tz2_path:     .ascii "/sys/class/thermal/thermal_zone2/temp\0"
prefix:       .ascii "tick "
space_eq:     .ascii " temp="
stress_lbl:   .ascii " stress="
drive_lbl:    .ascii " drive="
celsius:      .ascii "C\0"

# status characters
ch_dot:       .byte '.'
ch_colon:     .byte ':'
ch_bang:      .byte '!'
ch_hash:      .byte '#'

# ── BSS ────────────────────────────────────────────────────────────
.section .bss
   .align 8
tbuf:         .space 64
msr_buf:      .space 8
temp_buf:     .space 16            # ascii temp digits

# persistent state across ticks
fail_count:   .space 8            # consecutive open failures
cooldown:     .space 8            # remaining cooldown ticks
last_temp:    .space 8            # last valid temperature
last_stress:  .space 1            # last valid hw_stress

# ── Text ───────────────────────────────────────────────────────────
.section .text
.globl _start

# ══════════════════════════════════════════════════════════════════
# Entry
# ══════════════════════════════════════════════════════════════════
_start:
    # mmap soul at 0x200000
    movq $SYS_MMAP, %rax
    movq $SOUL_ADDR, %rdi
    movq $PAGE_SIZE, %rsi
    movq $PROT_RW, %rdx
    movq $MAP_FPA, %r10
    movq $-1, %r8
    xorq %r9, %r9
    syscall
    cmpq $-1, %rax
    je .die

    # zero soul
    movq $SOUL_ADDR, %rdi
    xorq %rax, %rax
    movq $12, %rcx
    rep stosq

    movq $SOUL_ADDR, %rbx
    movq $EXPECTED_TSC, S_EXPECTED(%rbx)

    rdtscp
    shlq $32, %rdx
    orq  %rdx, %rax
    movq %rax, S_LAST_TSC(%rbx)
    movq %rax, S_CUR_TSC(%rbx)
    call crc_store

    # init persistent state
    movq $0, fail_count(%rip)
    movq $0, cooldown(%rip)
    movq $0, last_temp(%rip)
    movb $0, last_stress(%rip)

    xorq %r12, %r12                 # tick counter

# ══════════════════════════════════════════════════════════════════
# Heartbeat
# ══════════════════════════════════════════════════════════════════
.tick:
    # ── Sense ─────────────────────────────────────────────────────
    incq %r12
    movq $SOUL_ADDR, %rbx
    movl %r12d, S_TICK(%rbx)

    rdtscp
    shlq $32, %rdx
    orq  %rdx, %rax
    movq %rax, S_CUR_TSC(%rbx)

    movq S_LAST_TSC(%rbx), %rcx
    subq %rcx, %rax
    movq %rax, S_ELAPSED(%rbx)

    # ── Compare (temperature-driven hw_stress) ────────────────────
    call sense_temperature          # → rax = degrees C, or -1

    cmpq $-1, %rax
    je  .cmp_keep                   # all paths failed → keep last

    # update last_temp
    movq %rax, last_temp(%rip)

    # classify hw_stress by temperature
    cmpq $TEMP_85, %rax
    jg  .cmp_s3
    cmpq $TEMP_80, %rax
    jge .cmp_s2
    cmpq $TEMP_70, %rax
    jge .cmp_s1
    xorq %rax, %rax                # stress 0
    jmp .cmp_set
.cmp_s1: movq $1, %rax; jmp .cmp_set
.cmp_s2: movq $2, %rax; jmp .cmp_set
.cmp_s3: movq $3, %rax
.cmp_set:
    movb %al, S_HW_STRESS(%rbx)
    movb %al, last_stress(%rip)
    jmp .cmp_done

.cmp_keep:
    # restore last known stress
    movzbq last_stress(%rip), %rax
    movb %al, S_HW_STRESS(%rbx)

.cmp_done:

    # ── React ─────────────────────────────────────────────────────
    movq S_CUR_TSC(%rbx), %rax
    movq %rax, S_LAST_TSC(%rbx)

    # ── Compute drive-adjusted expected_tsc ────────────────────────
    # base interval from hw_stress: 0→100ms, 1→250ms, 2→500ms, 3→1000ms
    movq $SOUL_ADDR, %rbx
    movzbq S_HW_STRESS(%rbx), %rax
    cmpq $3, %rax
    je   .base_1000
    cmpq $2, %rax
    je   .base_500
    cmpq $1, %rax
    je   .base_250
    movq $300000000, %rax            # 100ms @ 3GHz
    jmp  .base_set
.base_250:
    movq $750000000, %rax            # 250ms
    jmp  .base_set
.base_500:
    movq $1500000000, %rax           # 500ms
    jmp  .base_set
.base_1000:
    movq $3000000000, %rax           # 1000ms
.base_set:
    movq %rax, %r13                  # r13 = base_interval

    # drive_factor = (100 - drive) / 100
    # drive is int8 at S_DRIVE: positive → faster (factor < 1), negative → slower (factor > 1)
    # We compute: adjusted = base * (100 - drive) / 100
    movsbl S_DRIVE(%rbx), %eax      # sign-extend int8 to int64
    movq $100, %rcx
    subq %rax, %rcx                 # rcx = 100 - drive
    # clamp: if 100-drive < 50, use 50 (min 50ms); if > 200, use 200 (max 2000ms)
    cmpq $50, %rcx
    jge  .dfloor_ok
    movq $50, %rcx
.dfloor_ok:
    cmpq $200, %rcx
    jle  .dceil_ok
    movq $200, %rcx
.dceil_ok:
    # adjusted = base * (100 - drive) / 100
    movq %r13, %rax
    imulq %rcx, %rax                # rax = base * factor_numerator
    xorq %rdx, %rdx
    movq $100, %rcx
    divq %rcx                        # rax = adjusted interval
    movq %rax, S_EXPECTED(%rbx)

    # ── Record ────────────────────────────────────────────────────
    call crc_store

    # ── Verify ────────────────────────────────────────────────────
    call crc_check
    testq %rax, %rax
    jnz  .vfy_ok

    movq $SOUL_ADDR, %rdi
    xorq %rax, %rax
    movq $12, %rcx
    rep stosq
    movq $SOUL_ADDR, %rbx
    movq $EXPECTED_TSC, S_EXPECTED(%rbx)
    xorq %r12, %r12
    rdtscp
    shlq $32, %rdx
    orq  %rdx, %rax
    movq %rax, S_LAST_TSC(%rbx)
    movq %rax, S_CUR_TSC(%rbx)
    call crc_store
    jmp  .tick
.vfy_ok:

    # ── Output: status character ──────────────────────────────────
    movq $SOUL_ADDR, %rbx
    movzbq S_HW_STRESS(%rbx), %rcx

    # pick char based on stress and temp
    cmpq $3, %rcx
    je   .out_hash
    cmpq $1, %rcx
    jge  .out_bang
    # stress 0: check temp
    movq last_temp(%rip), %rax
    cmpq $TEMP_50, %rax
    jge  .out_colon
    leaq ch_dot(%rip), %rsi
    jmp  .out_char
.out_colon:
    leaq ch_colon(%rip), %rsi
    jmp  .out_char
.out_bang:
    leaq ch_bang(%rip), %rsi
    jmp  .out_char
.out_hash:
    leaq ch_hash(%rip), %rsi
.out_char:
    movq $SYS_WRITE, %rax
    movq $1, %rdi
    movq $1, %rdx
    syscall

    # every 100 ticks: "tick NNN temp=NNC stress=N"
    movq %r12, %rax
    xorq %rdx, %rdx
    movq $100, %rcx
    divq %rcx
    testq %rdx, %rdx
    jnz  .no_tick

    # build line in tbuf
    leaq tbuf(%rip), %rdi

    # "tick "
    leaq prefix(%rip), %rsi
    movq $5, %rcx
.pt: movb (%rsi),%al; movb %al,(%rdi); incq %rsi; incq %rdi; decq %rcx; jnz .pt

    # tick number
    movq %r12, %rax
    xorq %r8, %r8
.itoa1: xorq %rdx,%rdx; movq $10,%rcx; divq %rcx; pushq %rdx; incq %r8; testq %rax,%rax; jnz .itoa1
.otoa1: popq %rax; addb $'0',%al; movb %al,(%rdi); incq %rdi; decq %r8; jnz .otoa1

    # " temp="
    leaq space_eq(%rip), %rsi
    movq $6, %rcx
.pt2: movb (%rsi),%al; movb %al,(%rdi); incq %rsi; incq %rdi; decq %rcx; jnz .pt2

    # temperature number
    movq last_temp(%rip), %rax
    xorq %r8, %r8
.itoa2: xorq %rdx,%rdx; movq $10,%rcx; divq %rcx; pushq %rdx; incq %r8; testq %rax,%rax; jnz .itoa2
.otoa2: popq %rax; addb $'0',%al; movb %al,(%rdi); incq %rdi; decq %r8; jnz .otoa2

    # "C"
    movb $'C', (%rdi); incq %rdi

    # " stress="
    leaq stress_lbl(%rip), %rsi
    movq $8, %rcx
.pt3: movb (%rsi),%al; movb %al,(%rdi); incq %rsi; incq %rdi; decq %rcx; jnz .pt3

    # stress digit
    movq $SOUL_ADDR, %rbx
    movzbq S_HW_STRESS(%rbx), %rax
    addb $'0', %al
    movb %al, (%rdi); incq %rdi

    # " drive="
    leaq drive_lbl(%rip), %rsi
    movq $7, %rcx
.pt4: movb (%rsi),%al; movb %al,(%rdi); incq %rsi; incq %rdi; decq %rcx; jnz .pt4

    # drive value (signed int8, may be negative)
    movq $SOUL_ADDR, %rbx
    movsbl S_DRIVE(%rbx), %eax
    testq %rax, %rax
    jns  .drv_pos
    movb $'-', (%rdi); incq %rdi
    negq %rax
.drv_pos:
    xorq %r8, %r8
.itoa3: xorq %rdx,%rdx; movq $10,%rcx; divq %rcx; pushq %rdx; incq %r8; testq %rax,%rax; jnz .itoa3
.otoa3: popq %rax; addb $'0',%al; movb %al,(%rdi); incq %rdi; decq %r8; jnz .otoa3

    # newline
    movb $10, (%rdi); incq %rdi

    # write
    leaq tbuf(%rip), %rsi
    movq %rdi, %rax
    subq %rsi, %rax
    movq %rax, %rdx
    movq $SYS_WRITE, %rax
    movq $1, %rdi
    syscall
.no_tick:

    # ── Wait ──────────────────────────────────────────────────────
    movq $SOUL_ADDR, %rbx
.wait: pause
       rdtscp
       shlq $32, %rdx
       orq  %rdx, %rax
       subq S_LAST_TSC(%rbx), %rax
       cmpq S_EXPECTED(%rbx), %rax
       jl   .wait

    jmp  .tick

# ══════════════════════════════════════════════════════════════════
# sense_temperature
#   Returns rax = temperature in °C, or -1 on failure
#   Path A: /dev/cpu/0/msr (MSR 0x1A2 → TjMax, MSR 0x19C → reading)
#   Path B: /sys/class/thermal/thermal_zoneN/temp
#   Cooldown: 10 consecutive fails → skip 300 ticks
# ══════════════════════════════════════════════════════════════════
sense_temperature:
    pushq %rbx
    pushq %r12

    # check cooldown
    movq cooldown(%rip), %rax
    testq %rax, %rax
    jnz  .st_cooldown

    # ── Path A: MSR ──────────────────────────────────────────────
    leaq msr_path(%rip), %rdi
    movq $O_RDONLY, %rsi
    xorq %rdx, %rdx
    movq $SYS_OPEN, %rax
    syscall
    cmpq $-1, %rax
    je   .st_pathB

    # fd in r12
    movq %rax, %r12

    # lseek to MSR_TEMPERATURE_TARGET (0x1A2)
    movq $SYS_LSEEK, %rax
    movq %r12, %rdi
    movq $0x1A2, %rsi               # offset
    movq $SEEK_SET, %rdx            # whence
    syscall

    # read 8 bytes
    movq $SYS_READ, %rax
    movq %r12, %rdi
    leaq msr_buf(%rip), %rsi
    movq $8, %rdx
    syscall
    cmpq $8, %rax
    jne  .st_msr_close_fail

    # parse TjMax from bits[23:16]
    movq msr_buf(%rip), %rax
    shrq $16, %rax
    andq $0xFF, %rax                 # TjMax
    movq %rax, %rbx                  # rbx = TjMax

    # lseek to IA32_THERM_STATUS (0x19C)
    movq $SYS_LSEEK, %rax
    movq %r12, %rdi
    movq $0x19C, %rsi
    movq $SEEK_SET, %rdx
    syscall

    # read 8 bytes
    movq $SYS_READ, %rax
    movq %r12, %rdi
    leaq msr_buf(%rip), %rsi
    movq $8, %rdx
    syscall
    cmpq $8, %rax
    jne  .st_msr_close_fail

    # close fd
    movq $SYS_CLOSE, %rax
    movq %r12, %rdi
    syscall

    # parse DigitalReading from bits[22:16]
    movq msr_buf(%rip), %rax
    shrq $16, %rax
    andq $0x7F, %rax                 # bits[22:16] = 7 bits
    # temp = TjMax - DigitalReading
    subq %rax, %rbx
    movq %rbx, %rax

    # reset fail counter on success
    movq $0, fail_count(%rip)
    jmp  .st_done

.st_msr_close_fail:
    movq $SYS_CLOSE, %rax
    movq %r12, %rdi
    syscall
    # fall through to path B

    # ── Path B: sysfs thermal_zone (try all, pick highest) ────────
.st_pathB:
    xorq %r12, %r12                # best temp = 0 (0 means no valid reading yet)
    leaq tz0_path(%rip), %rdi
    call .st_read_sysfs
    cmpq $-1, %rax
    je  .st_tz1
    movq %rax, %r12
.st_tz1:
    leaq tz1_path(%rip), %rdi
    call .st_read_sysfs
    cmpq $-1, %rax
    je  .st_tz2
    cmpq %r12, %rax
    cmovg %rax, %r12
.st_tz2:
    leaq tz2_path(%rip), %rdi
    call .st_read_sysfs
    cmpq $-1, %rax
    je  .st_tz_done
    cmpq %r12, %rax
    cmovg %rax, %r12
.st_tz_done:
    testq %r12, %r12
    jz   .st_all_fail

    # success: r12 = highest millidegrees
    movq %r12, %rax
    xorq %rdx, %rdx
    movq $1000, %rcx
    divq %rcx                        # rax = degrees C
    movq $0, fail_count(%rip)
    jmp  .st_done

.st_all_fail:
    incq fail_count(%rip)
    movq fail_count(%rip), %rax
    cmpq $FAIL_LIMIT, %rax
    jge  .st_set_cooldown
    movq $-1, %rax
    jmp  .st_done
.st_set_cooldown:
    movq $COOLDOWN_TICKS, cooldown(%rip)
    movq $-1, %rax
    jmp  .st_done

.st_cooldown:
    decq cooldown(%rip)
    movq $-1, %rax
    jmp  .st_done

.st_done:
    popq %r12
    popq %rbx
    retq

# .st_read_sysfs: open file at rdi, read ascii number, close, return value in rax
#   Returns rax = millidegrees, or -1 on failure
.st_read_sysfs:
    pushq %rbx
    pushq %r13
    movq $O_RDONLY, %rsi
    xorq %rdx, %rdx
    movq $SYS_OPEN, %rax
    syscall
    cmpq $-1, %rax
    je   .srs_fail
    movq %rax, %rbx                 # fd

    # read up to 16 bytes
    movq $SYS_READ, %rax
    movq %rbx, %rdi
    leaq temp_buf(%rip), %rsi
    movq $16, %rdx
    syscall
    movq %rax, %r13                 # bytes read

    # close
    movq $SYS_CLOSE, %rax
    movq %rbx, %rdi
    syscall

    # if read 0 bytes, fail
    testq %r13, %r13
    jle  .srs_fail

    # parse ascii decimal to integer
    leaq temp_buf(%rip), %rsi
    xorq %rax, %rax                 # accumulator
.srs_digit:
    movzbl (%rsi), %ecx
    subb $'0', %cl
    cmpb $9, %cl
    ja   .srs_done_parse
    imulq $10, %rax, %rax
    movzbq %cl, %rcx
    addq %rcx, %rax
    incq %rsi
    jmp  .srs_digit
.srs_done_parse:
    popq %r13
    popq %rbx
    retq

.srs_fail:
    movq $-1, %rax
    popq %r13
    popq %rbx
    retq

# ══════════════════════════════════════════════════════════════════
# CRC32 helpers — software, polynomial 0xEDB88320
# ══════════════════════════════════════════════════════════════════
crc_store:
    pushq %rbx
    movq  $SOUL_ADDR, %rbx
    movb  S_DRIVE(%rbx), %r9b        # save drive
    movl  $0, S_CRC(%rbx)
    movb  $0, S_DRIVE(%rbx)          # drive excluded from checksum
    movq  %rbx, %rsi
    movq  $96, %rcx
    xorl  %eax, %eax
    notl  %eax
.cs_b: movzbl (%rsi), %edi; xorl %edi, %eax
       movq $8, %rdx
.cs_bit: shrl $1, %eax; jnc .cs_nb; xorl $0xEDB88320, %eax
.cs_nb:  decq %rdx; jnz .cs_bit
       incq %rsi; decq %rcx; jnz .cs_b
    notl  %eax
    movl  %eax, S_CRC(%rbx)
    movb  %r9b, S_DRIVE(%rbx)          # restore drive
    popq  %rbx
    retq

crc_check:
    pushq %rbx
    movq  $SOUL_ADDR, %rbx
    movl  S_CRC(%rbx), %r8d
    movb  S_DRIVE(%rbx), %r9b        # save drive
    movl  $0, S_CRC(%rbx)
    movb  $0, S_DRIVE(%rbx)          # drive excluded from checksum
    movq  %rbx, %rsi
    movq  $96, %rcx
    xorl  %eax, %eax
    notl  %eax
.cc_b: movzbl (%rsi), %edi; xorl %edi, %eax
       movq $8, %rdx
.cc_bit: shrl $1, %eax; jnc .cc_nb; xorl $0xEDB88320, %eax
.cc_nb:  decq %rdx; jnz .cc_bit
       incq %rsi; decq %rcx; jnz .cc_b
    notl  %eax
    movl  %r8d, S_CRC(%rbx)
    movb  %r9b, S_DRIVE(%rbx)          # restore drive
    cmpl  %r8d, %eax
    sete  %al
    movzbq %al, %rax
    popq  %rbx
    retq

.die:
    movq $SYS_EXIT, %rax
    movq $1, %rdi
    syscall
