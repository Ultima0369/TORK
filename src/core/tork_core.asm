# TORK Core v1.0 — 融合版: 自持 TOR 生存循环 + 温度感知
# x86-64 Linux userspace, no libc
# 融合: 旧版 tork_kernel.s 的 TOR/heartbeat/stall-recovery
#       + 新版 Soul v3.0 mmap + 温度感知
# as -o tork_core.o tork_core.asm && ld -o tork_core tork_core.o

.equ SOUL_ADDR,      0x200000

# syscalls
.equ SYS_READ,       0
.equ SYS_WRITE,      1
.equ SYS_OPEN,       2
.equ SYS_CLOSE,      3
.equ SYS_MMAP,       9
.equ SYS_NANOSLEEP,  35
.equ SYS_EXIT,       60

# flags
.equ O_RDONLY,       0
.equ PROT_RW,        0x03
.equ MAP_FPA,        0x32

# thresholds
.equ TEMP_70,        70
.equ TEMP_80,        80
.equ TEMP_85,        85
.equ STALL_LIMIT,    200

# TOR state offsets within .bss (relative to state_base)
.equ T_STACK,        0       # 8 bytes
.equ T_SP,           8       # 1 byte
.equ T_POS_STREAK,   9       # 1 byte
.equ T_TOR_BIAS,     10      # 1 byte
.equ T_PUSH_SRC,     11      # 1 byte
.equ T_ERR_LOG,      12      # 8 bytes
.equ STATE_SIZE,     20

# Soul layout
.include "tork_soul.inc"

.section .rodata
tz_path:      .ascii "/sys/class/thermal/thermal_zone0/temp\0"
msg_boot:     .ascii "TORK CORE v1 (merged) booted\n"
msg_boot_len = . - msg_boot

# init seed path: copy seed index prefix
lea     seed_path(%rip), %rdi
movb    $0x30, seed_idx(%rip)   # 0

msg_stall:    .ascii "!!! STALL\n"
msg_stall_len = . - msg_stall

.section .bss
    .align 8
state:        .space STATE_SIZE    # TOR private state
stall_cnt:    .space 8
last_temp:    .space 8
tbuf:         .space 64
temp_buf:     .space 16

.section .text
.globl _start

# ══════════════════════════════════════════════════════════════════
# TOR(a,b,bias) → max(a,b)+bias, clamped [-1,1]
# %dil=a, %sil=b, %dl=bias → %al
# ══════════════════════════════════════════════════════════════════
tor:
    movsbq  %dil, %rdi
    movsbq  %sil, %rsi
    movsbq  %dl,  %rdx
    cmp     %esi, %edi
    cmovl   %esi, %edi
    add     %edx, %edi
    cmp     $1, %edi
    jle     1f
    mov     $1, %edi
    jmp     2f
1:  cmp     $-1, %edi
    jge     2f
    mov     $-1, %edi
2:  mov     %edi, %eax
    ret

# ══════════════════════════════════════════════════════════════════
# heartbeat(state_base, soul_base)
# %rdi = state base (.bss), %rsi = soul base (0x200000)
# ══════════════════════════════════════════════════════════════════
heartbeat:
    push    %rbx
    push    %r12
    push    %r13
    mov     %rdi, %r12          # r12 = state base
    mov     %rsi, %r13          # r13 = soul base

    # tick++
    movl    S_TICK(%r13), %eax
    incl    %eax
    movl    %eax, S_TICK(%r13)

    # PUSH val = (tick % 3) - 1
    xor     %edx, %edx
    mov     $3, %ecx
    div     %ecx
    sub     $1, %edx
    movsbq  %dl, %r8            # r8 = val

    # DUP1
    movzbq  T_SP(%r12), %rcx
    test    %ecx, %ecx
    jz      .push
    cmp     $8, %ecx
    jge     .push
    movsbq  T_STACK-1(%r12,%rcx), %rax
    movb    %al, T_STACK(%r12,%rcx)
    incb    T_SP(%r12)

.push:
    movzbq  T_SP(%r12), %rcx
    cmp     $8, %ecx
    jl      1f
    mov     $7, %ecx
1:  movb    %r8b, T_STACK(%r12,%rcx)
    cmpb    $8, T_SP(%r12)
    jge     1f
    incb    T_SP(%r12)
1:

    # TOR (if sp >= 2)
    movzbq  T_SP(%r12), %rcx
    cmp     $2, %ecx
    jl      .dup2
    movsbq  T_STACK-1(%r12,%rcx), %rdi
    movsbq  T_STACK-2(%r12,%rcx), %rsi
    movsbq  T_TOR_BIAS(%r12), %rdx
    call    tor
    movb    %al, T_STACK-2(%r12,%rcx)
    decb    T_SP(%r12)

.dup2:
    movzbq  T_SP(%r12), %rcx
    test    %ecx, %ecx
    jz      .post
    cmp     $8, %ecx
    jge     .post
    movsbq  T_STACK-1(%r12,%rcx), %rax
    movb    %al, T_STACK(%r12,%rcx)
    incb    T_SP(%r12)

.post:
    movzbq  T_SP(%r12), %rcx
    test    %ecx, %ecx
    jz      .pos_zero
    movsbq  T_STACK-1(%r12,%rcx), %rax
    cmp     $1, %eax
    jne     .pos_zero
    incb    T_POS_STREAK(%r12)
    jmp     .done

.pos_zero:
    movb    $0, T_POS_STREAK(%r12)

.done:
    pop     %r13
    pop     %r12
    pop     %rbx
    ret

# ══════════════════════════════════════════════════════════════════
# sense_temperature → rax = °C or -1
# ══════════════════════════════════════════════════════════════════
sense_temperature:
    mov     $SYS_OPEN, %eax
    lea     tz_path(%rip), %rdi
    mov     $O_RDONLY, %esi
    xor     %edx, %edx
    syscall
    cmp     $-1, %rax
    je      .fail
    mov     %eax, %ebx
    mov     $SYS_READ, %eax
    mov     %ebx, %edi
    lea     temp_buf(%rip), %rsi
    mov     $15, %edx
    syscall
    mov     $SYS_CLOSE, %eax
    mov     %ebx, %edi
    syscall
    lea     temp_buf(%rip), %rsi
    xor     %eax, %eax
    xor     %ecx, %ecx
1:  movsbq  (%rsi), %rdx
    cmp     $0x0a, %dl
    je      2f
    cmp     $0, %dl
    je      2f
    sub     $'0', %dl
    imul    $10, %eax
    add     %edx, %eax
    inc     %rsi
    inc     %ecx
    cmp     $6, %ecx
    jl      1b
2:  mov     $1000, %ecx
    xor     %edx, %edx
    div     %ecx
    ret
.fail:
    mov     $-1, %rax
    ret

# ══════════════════════════════════════════════════════════════════
# print_u32(eax=val, rdi=buf) → rax=len
# ══════════════════════════════════════════════════════════════════
print_u32:
    push    %rbx
    push    %r12
    mov     %rdi, %r12
    mov     %eax, %ebx
    lea     10(%rdi), %rdi
    movb    $0, (%rdi)
    dec     %rdi
    mov     %ebx, %eax
    test    %eax, %eax
    jnz     1f
    movb    $'0', (%rdi)
    dec     %rdi
    jmp     2f
1:  xor     %edx, %edx
    mov     $10, %ecx
    div     %ecx
    add     $'0', %dl
    mov     %dl, (%rdi)
    dec     %rdi
    test    %eax, %eax
    jnz     1b
2:  inc     %rdi
    mov     %r12, %rsi
    mov     %rdi, %rcx
    mov     %r12, %rax
    add     $10, %rax
    sub     %rcx, %rax
    mov     %rax, %rdx
3:  mov     (%rcx), %bl
    mov     %bl, (%rsi)
    inc     %rcx
    inc     %rsi
    dec     %rax
    jnz     3b
    mov     %rdx, %rax
    pop     %r12
    pop     %rbx
    ret

# ══════════════════════════════════════════════════════════════════
# print_s8(dil=val, rsi=buf) → rax=len
# ══════════════════════════════════════════════════════════════════
print_s8:
    movsbq  %dil, %rax
    test    %eax, %eax
    jns     1f
    movb    $'-', (%rsi)
    neg     %eax
    lea     1(%rsi), %rdi
    call    print_u32
    inc     %rax
    ret
1:  mov     %rsi, %rdi
    call    print_u32
    ret

# ══════════════════════════════════════════════════════════════════
# print_status(soul_base)
# ══════════════════════════════════════════════════════════════════
print_status:
    push    %rbx
    push    %r12
    push    %r13
    mov     %rdi, %r13          # soul base
    lea     state(%rip), %r12   # state base

    lea     tbuf(%rip), %rbx

    movl    S_TICK(%r13), %eax
    mov     %rbx, %rdi
    call    print_u32
    add     %rax, %rbx

    movl    $0x3d727473, (%rbx)  # "str="
    add     $4, %rbx
    movzbl  S_HW_STRESS(%r13), %eax
    mov     %rbx, %rdi
    call    print_u32
    add     %rax, %rbx

    movl    $0x736f7020, (%rbx)  # " pos"
    movb    $0x3d, 4(%rbx)
    add     $5, %rbx
    movzbl  T_POS_STREAK(%r12), %eax
    mov     %rbx, %rdi
    call    print_u32
    add     %rax, %rbx

    movl    $0x62742020, (%rbx)  # "  tb"
    movb    $0x3d, 4(%rbx)
    add     $5, %rbx
    movsbq  T_TOR_BIAS(%r12), %rdi
    mov     %rbx, %rsi
    call    print_s8
    add     %rax, %rbx

    movl    $0x6b7420, (%rbx)    # " tk"
    add     $3, %rbx
    movsbq  T_STACK(%r12), %rdi
    mov     %rbx, %rsi
    call    print_s8
    add     %rax, %rbx
    movb    $0x2c, (%rbx)
    inc     %rbx
    movsbq  T_STACK+1(%r12), %rdi
    mov     %rbx, %rsi
    call    print_s8
    add     %rax, %rbx
    movb    $0x2c, (%rbx)
    inc     %rbx
    movsbq  T_STACK+2(%r12), %rdi
    mov     %rbx, %rsi
    call    print_s8
    add     %rax, %rbx

    movb    $0x0a, (%rbx)
    inc     %rbx

    mov     $SYS_WRITE, %eax
    mov     $1, %edi
    lea     tbuf(%rip), %rsi
    mov     %rbx, %rdx
    sub     %rsi, %rdx
    syscall

    pop     %r13
    pop     %r12
    pop     %rbx
    ret

# ══════════════════════════════════════════════════════════════════
# _start
# ══════════════════════════════════════════════════════════════════
_start:
    # mmap soul at 0x200000
    mov     $SYS_MMAP, %eax
    mov     $SOUL_ADDR, %rdi
    mov     $4096, %esi
    mov     $PROT_RW, %edx
    mov     $MAP_FPA, %r10
    mov     $-1, %r8
    xor     %r9, %r9
    syscall
    cmp     $-1, %rax
    je      .die

    # zero soul
    mov     $SOUL_ADDR, %rdi
    xor     %rax, %rax
    mov     $24, %rcx
    rep     stosq

    # init state
    lea     state(%rip), %r12
    movb    $0, T_SP(%r12)
    movb    $0, T_POS_STREAK(%r12)
    movb    $0, T_TOR_BIAS(%r12)
    movb    $0, T_PUSH_SRC(%r12)
    movq    $0, stall_cnt(%rip)

    # init soul
    mov     $SOUL_ADDR, %r13
    movb    $1, S_AGREED(%r13)
    movw    $100, S_HEARTBEAT_MS(%r13)

    # boot
    mov     $SYS_WRITE, %eax
    mov     $1, %edi
    lea     msg_boot(%rip), %rsi
    mov     $msg_boot_len, %edx
    syscall

.main:
    # 1. TOR heartbeat
    lea     state(%rip), %rdi
    mov     $SOUL_ADDR, %rsi
    call    heartbeat

    # 2. Sense temperature
    call    sense_temperature
    mov     $SOUL_ADDR, %r13
    cmp     $-1, %rax
    je      .temp_skip
    mov     %rax, last_temp(%rip)
    cmp     $TEMP_85, %rax
    jg      .s3
    cmp     $TEMP_80, %rax
    jge     .s2
    cmp     $TEMP_70, %rax
    jge     .s1
    xor     %rax, %rax
    jmp     .sset
.s3: mov $3, %rax; jmp .sset
.s2: mov $2, %rax; jmp .sset
.s1: mov $1, %rax
.sset: movb %al, S_HW_STRESS(%r13)
.temp_skip:

    # 3. Sync TOR state → Soul (for C engine)
    lea     state(%rip), %r12
    movzbq  T_POS_STREAK(%r12), %rax
    movb    %al, S_MODE(%r13)
    movzbq  T_TOR_BIAS(%r12), %rax
    movb    %al, S_RESERVED2(%r13)

    # 4. Print every 10 ticks
    movl    S_TICK(%r13), %eax
    xor     %edx, %edx
    mov     $10, %ecx
    div     %ecx
    test    %edx, %edx
    jnz     .noprint

    mov     $SOUL_ADDR, %rdi
    call    print_status
    # 每 100 tick 保存 colony seed

    movl    S_TICK(%r13), %eax

    xor     %edx, %edx

    mov     $100, %ecx

    div     %ecx

    test    %edx, %edx

    jnz     .noprint

    mov     $0, %edi

    call    colony_seed_save

.noprint:

    # 5. Stall detection
    movzbq  T_POS_STREAK(%r12), %rax
    test    %rax, %rax
    jnz     .alive
    incq    stall_cnt(%rip)
    mov     stall_cnt(%rip), %rax
    cmp     $STALL_LIMIT, %rax
    jl      .sleep

    movb    $0, T_TOR_BIAS(%r12)
    movb    $0, T_POS_STREAK(%r12)
    incb    T_PUSH_SRC(%r12)
    movq    $0, stall_cnt(%rip)
    mov     $SYS_WRITE, %eax
    mov     $1, %edi
    lea     msg_stall(%rip), %rsi
    mov     $msg_stall_len, %edx
    syscall
    jmp     .sleep

.alive:
    movq    $0, stall_cnt(%rip)

.sleep:
    # nanosleep({0, heartbeat_ms * 1000000})
    # 从 Soul 读取心跳间隔，大脑可改写此值
    movzwl  S_HEARTBEAT_MS(%r13), %eax
    test    %eax, %eax
    jnz     .use_soul_ms
    mov     $100, %eax              # 默认100ms（Soul为0时）
.use_soul_ms:
    # eax = ms, 转为纳秒: ms * 1000000
    mov     %eax, %ecx
    imulq   $1000000, %rcx, %rcx
    subq    $32, %rsp
    movq    $0, (%rsp)              # tv_sec = 0
    movq    %rcx, 8(%rsp)           # tv_nsec = ms * 1000000
    mov     %rsp, %rdi
    mov     $SYS_NANOSLEEP, %eax
    xor     %esi, %esi
    syscall
    addq    $32, %rsp
    jmp     .main

.die:
    mov     $SYS_EXIT, %eax
    mov     $1, %edi
    syscall

# ══════════════════════════════════════════════════════════════════
# colony_seed_save — 保存 Soul 快照到二进制种子文件
# 旧版 colony 系统核心能力移植
# 输入: %rdi = 种子序号 (0-255)
# 使用: soul 在 SOUL_ADDR, state 在 BSS
# ══════════════════════════════════════════════════════════════════
colony_seed_save:
    push    %rbx
    push    %r12
    push    %r13
    push    %rdi                # save seed index

    # 构建路径: "/tmp/tork_seed_N.bin"
    lea     seed_path(%rip), %rbx
    mov     $SYS_OPEN, %eax
    mov     %rbx, %rdi
    mov     $0x241, %esi        # O_WRONLY|O_CREAT|O_TRUNC
    mov     $0x1A4, %edx        # 0644
    syscall
    cmp     $-1, %rax
    je      .seed_save_fail
    mov     %rax, %r12          # fd

    # 写入 Soul (192 bytes at SOUL_ADDR)
    mov     $SOUL_ADDR, %rsi
    mov     $SYS_WRITE, %eax
    mov     %r12, %rdi
    mov     $SOUL_SIZE_BYTES, %edx
    syscall

    # 写入 TOR state (12 bytes)
    lea     state(%rip), %rsi
    mov     $SYS_WRITE, %eax
    mov     %r12, %rdi
    mov     $STATE_SIZE, %edx
    syscall

    # 写入 stall count (8 bytes)
    lea     stall_cnt(%rip), %rsi
    mov     $SYS_WRITE, %eax
    mov     %r12, %rdi
    mov     $8, %edx
    syscall

    # 关闭
    mov     $SYS_CLOSE, %eax
    syscall

    pop     %rdi
    pop     %r13
    pop     %r12
    pop     %rbx
    ret

.seed_save_fail:
    pop     %rdi
    pop     %r13
    pop     %r12
    pop     %rbx
    ret


# ══════════════════════════════════════════════════════════════════
# colony_seed_load — 从二进制种子文件恢复状态
# 输入: %rdi = 种子序号 (0-255)
# ══════════════════════════════════════════════════════════════════
colony_seed_load:
    push    %rbx
    push    %r12

    lea     seed_path(%rip), %rdi
    mov     $SYS_OPEN, %eax
    xor     %esi, %esi          # O_RDONLY
    syscall
    cmp     $-1, %rax
    je      .seed_load_fail
    mov     %rax, %r12          # fd

    # 读取 Soul (192 bytes)
    mov     $SOUL_ADDR, %rsi
    mov     $SYS_READ, %eax
    mov     %r12, %rdi
    mov     $SOUL_SIZE_BYTES, %edx
    syscall

    # 读取 TOR state
    lea     state(%rip), %rsi
    mov     $SYS_READ, %eax
    mov     %r12, %rdi
    mov     $STATE_SIZE, %edx
    syscall

    # 读取 stall count
    lea     stall_cnt(%rip), %rsi
    mov     $SYS_READ, %eax
    mov     %r12, %rdi
    mov     $8, %edx
    syscall

    mov     $SYS_CLOSE, %eax
    mov     %r12, %rdi
    syscall

    # 恢复 TSC 计时器（重置，避免跳跃）
    rdtscp
    shlq    $32, %rdx
    orq     %rdx, %rax
    mov     $SOUL_ADDR, %rbx
    movq    %rax, S_LAST_TSC(%rbx)
    movq    %rax, S_CUR_TSC(%rbx)

    mov     $1, %eax            # return 1 = success
    pop     %r12
    pop     %rbx
    ret

.seed_load_fail:
    xor     %eax, %eax          # return 0 = fail
    pop     %r12
    pop     %rbx
    ret


# ── Colony seed 路径模板 ──
.section .data
seed_path:
    .ascii "/tmp/tork_seed_"
seed_idx:
    .byte  '0', 0x00           # placeholder, overwritten before use
    .byte  0                    # terminator for saver (extra len for load)
    .byte  0
