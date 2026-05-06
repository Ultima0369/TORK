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
.equ SYS_MPROTECT,   10
.equ SYS_FCNTL,     72
.equ F_SETFL,       4
.equ O_NONBLOCK,    0x800

# flags
.equ O_RDONLY,       0
.equ PROT_RW,        0x03
.equ MAP_FPA,        0x32

# thresholds
.equ TEMP_70,        70
.equ TEMP_80,        80
.equ TEMP_85,        85
.equ STALL_LIMIT,    200

# CRC32 校验范围: Soul 前 (SOUL_SIZE_BYTES - 4) 字节，CRC 字段在 S_CRC
.equ CRC_RANGE,      204

# 时序防御: 正常频率下两次心跳间最低 TSC 增量
# 2GHz × 100ms × 50%安全边际 = 100,000,000 cycles
.equ TSC_SAFE_MIN,  100000000

# ══════════════════════════════════════════════════════════════════
# 熔断铁律 Section 1: 核心物理边界定义
# 三个受保护区域，原始字节不可被非授权修改
# ══════════════════════════════════════════════════════════════════
# 代码段边界: _start → .die (由链接器填充，运行时计算)
.equ CODE_START,     0           # 运行时由 _start 填入
.equ TOR_STATE_SIZE, 20          # .bss TOR 私有状态 20 字节

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


msg_stall:    .ascii "!!! STALL\n"
msg_stall_len = . - msg_stall

msg_fuse:     .ascii "!!! FUSE: self-destruct (CRC32 fail x3)\n"
msg_fuse_len = . - msg_fuse

# 铁律 Section 3: syslog 通知消息 + /dev/log 路径
msg_syslog:  .ascii "TORK FUSE: tamper detected — Soul+TOR zeroed, page locked\n"
msg_syslog_len = . - msg_syslog
msg_devlog:  .ascii "/dev/log\0"

.section .bss
    .align 8
fuse_cnt:   .quad 0              # 熔断计数器（连续 CRC 失败次数）
    .align 8
state:        .space STATE_SIZE    # TOR private state
stall_cnt:    .space 8
last_temp:    .space 8
tbuf:         .space 64
temp_buf:     .space 16
    .align 8
code_start_addr: .quad 0          # 铁律S1: _start 实际地址
code_end_addr:   .quad 0          # 铁律S1: .die 实际地址
hb_pipe_confirmed: .quad 0        # 铁律S2: 管道确认的心跳值
hb_pipe_buf:    .space 4         # 铁律S2: 管道读取缓冲区
last_tsc:          .quad 0        # 时序防御: 上次心跳 TSC
timing_fault_cnt:  .quad 0        # 时序防御: 降频异常计数

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

    # tick++ (atomic)
    lock incl S_TICK(%r13)

    # rdtsc: 记录时间戳到 Soul
    movq    S_CUR_TSC(%r13), %rax
    movq    %rax, S_LAST_TSC(%r13)     # prev → last
    rdtsc
    shlq    $32, %rdx
    orq     %rdx, %rax
    movq    %rax, S_CUR_TSC(%r13)      # current → cur

    # PUSH val = (tick % 3) - 1
    movl    S_TICK(%r13), %eax
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
    test    %rax, %rax
    js      .fail
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
    # 铁律 Section 1: 保存代码段物理边界
    lea     _start(%rip), %rax
    movq    %rax, code_start_addr(%rip)
    lea     .die(%rip), %rax
    movq    %rax, code_end_addr(%rip)

    # mmap soul at 0x200000
    mov     $SYS_MMAP, %eax
    mov     $SOUL_ADDR, %rdi
    mov     $4096, %esi
    mov     $PROT_RW, %edx
    mov     $MAP_FPA, %r10
    mov     $-1, %r8
    xor     %r9, %r9
    syscall
    test    %rax, %rax
    js      .die

    # zero soul (208 bytes = 26 qwords)
    mov     $SOUL_ADDR, %rdi
    xor     %rax, %rax
    mov     $26, %rcx
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
    # init seed index
    movb    $0x30, seed_idx(%rip)   # seed_idx = '0'

    # 初始化 CRC: 计算 Soul CRC32 并写入 S_CRC
    call    soul_crc32_update

    # boot
    mov     $SYS_WRITE, %eax
    mov     $1, %edi
    lea     msg_boot(%rip), %rsi
    mov     $msg_boot_len, %edx
    syscall

    # 铁律 Section 2: 将 stdin (fd=0) 设为非阻塞模式
    # 用于管道双通道心跳确认
    mov     $SYS_FCNTL, %eax
    xor     %edi, %edi          # fd = 0 (stdin)
    mov     $F_SETFL, %esi      # cmd = F_SETFL
    mov     $O_NONBLOCK, %edx   # arg = O_NONBLOCK
    syscall

.main:
    # 0a. 铁律 CRC32 校验 — 验证 nanosleep 期间 Soul 是否被篡改
    #     时序: 修改→CRC更新→nanosleep→CRC验证→修改→...
    #     nanosleep 是唯一篡改窗口，验证必须先于更新
    mov     $SOUL_ADDR, %r13
    call    soul_crc32_verify
    test    %eax, %eax
    jnz     .crc_ok
    # CRC 失败: 递增熔断计数
    incq    fuse_cnt(%rip)
    movq    fuse_cnt(%rip), %rax
    cmp     $3, %rax
    jge     fuse_self_destruct          # 3次连续失败 → 熔断
    jmp     .crc_pass                   # 未达3次，跳过但继续
.crc_ok:
    movq    $0, fuse_cnt(%rip)          # CRC 通过，重置计数
.crc_pass:

    # ── 时序防御：检测 CPU 降频攻击 ──────────────────────
    rdtsc
    shlq    $32, %rdx
    orq     %rdx, %rax
    movq    %rax, %r14              # r14 = 当前 TSC

    movq    last_tsc(%rip), %r15
    subq    %r15, %r14              # r14 = delta TSC

    rdtsc
    shlq    $32, %rdx
    orq     %rdx, %rax
    movq    %rax, last_tsc(%rip)    # 更新 last_tsc

    movq    $TSC_SAFE_MIN, %r15
    cmpq    %r15, %r14
    jae     .timing_ok              # delta >= 阈值 → 正常

    incq    timing_fault_cnt(%rip)
    movq    timing_fault_cnt(%rip), %rax
    cmpq    $3, %rax
    jl      .timing_ok              # 连续 < 3 次，暂不熔断

    jmp     fuse_self_destruct      # 连续 3 次异常 → 自毁

.timing_ok:
    movq    $0, timing_fault_cnt(%rip)

    # 1. TOR heartbeat
    lea     state(%rip), %rdi
    mov     $SOUL_ADDR, %rsi
    call    heartbeat

    # 2. Sense temperature
    call    sense_temperature
    mov     $SOUL_ADDR, %r13
    test    %rax, %rax
    js      .temp_skip
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
    # 铁律: 所有 Soul 修改完成，更新 CRC 后再进入 nanosleep
    # nanosleep 是唯一篡改窗口，下一个 tick 开头验证 CRC
    mov     $SOUL_ADDR, %r13
    call    soul_crc32_update


    # 铁律 Section 2: 从 stdin 管道读取 C 引擎确认的心跳值
    # 格式: "HB" (2B magic) + heartbeat_ms (2B LE)
    mov     $SYS_READ, %eax
    xor     %edi, %edi          # fd = 0 (stdin)
    lea     hb_pipe_buf(%rip), %rsi
    mov     $4, %edx            # 读取 4 字节
    syscall
    test    %rax, %rax
    js      1f                  # 无数据 (EAGAIN) → 跳过
    cmp     $4, %eax
    jne     1f                  # 不足4字节 → 跳过
    # 验证 magic "HB"
    movzwl  hb_pipe_buf(%rip), %ecx
    cmpw    $0x4248, %cx        # "HB" little-endian
    jne     1f                  # magic 不匹配 → 跳过
    # 更新 hb_pipe_confirmed
    movzwl  hb_pipe_buf+2(%rip), %ecx
    movq    %rcx, hb_pipe_confirmed(%rip)
1:
    # 铁律 Section 2: 心跳双通道确认
    # C 引擎写 S_HEARTBEAT_MS 时必须同时通过管道发送确认
    # 内核交叉验证: Soul 值 vs 管道确认值
    # 不匹配 → 拒绝新值，保持上次安全心跳
    movzwl  S_HEARTBEAT_MS(%r13), %eax
    movq    hb_pipe_confirmed(%rip), %rcx
    test    %rcx, %rcx
    jz      .hb_first                   # 首次无确认值，直接使用
    cmp     %rax, %rcx
    je      .hb_confirmed               # 匹配 → 使用 Soul 值
    # 不匹配: 回退到上次安全值 (低16位)
    mov     %ecx, %eax
.hb_confirmed:
.hb_first:
    test    %eax, %eax
    jnz     .use_soul_ms
    mov     $100, %eax              # 默认100ms（Soul为0时）
.use_soul_ms:
    # eax = ms.  Split: sec = ms / 1000, remainder_ms = ms % 1000
    # nsec = remainder_ms * 1000000
    xor     %edx, %edx
    mov     $1000, %ecx
    div     %ecx                    # eax = sec, edx = remainder_ms
    mov     %eax, %r8d              # r8 = tv_sec
    mov     %edx, %ecx
    imulq   $1000000, %rcx, %rcx   # rcx = tv_nsec (remainder_ms * 1M, always < 1B)
    subq    $32, %rsp
    movq    %r8, (%rsp)             # tv_sec = ms / 1000
    movq    %rcx, 8(%rsp)           # tv_nsec = (ms % 1000) * 1000000
    mov     %rsp, %rdi
    mov     $SYS_NANOSLEEP, %eax
    xor     %esi, %esi
    syscall
    addq    $32, %rsp
    jmp     .main

# ══════════════════════════════════════════════════════════════════
# crc32_byte — 单字节 CRC32 更新
# 输入: ebx = 当前 CRC, dil = 字节
# 输出: ebx = 更新后 CRC
# 保留: ecx, rdx, rdi, rsi (调用者可能使用)
# 多项式: 0xEDB88320 (reflected)
# ══════════════════════════════════════════════════════════════════
crc32_byte:
    push    %rcx
    xor     %dil, %bl
    mov     $8, %ecx
1:  shr     $1, %ebx
    jnc     2f
    xor     $0xEDB88320, %ebx
2:  dec     %ecx
    jnz     1b
    pop     %rcx
    ret

# ══════════════════════════════════════════════════════════════════
# soul_crc32_verify — 校验 Soul CRC32
# 输入: r13 = soul base
# 输出: eax = 1 (pass) / 0 (fail)
# 校验范围: Soul 前 CRC_RANGE 字节，CRC 在 S_CRC
# 内联 CRC32 避免寄存器冲突
# ══════════════════════════════════════════════════════════════════
soul_crc32_verify:
    push    %rbx
    push    %r12
    mov     $0xFFFFFFFF, %ebx          # CRC init
    xor     %r12d, %r12d               # offset = 0
.crc_loop:
    cmp     $CRC_RANGE, %r12d
    jge     .crc_done
    # 跳过 CRC 字段 (S_CRC=0x28, 4字节)
    cmp     $S_CRC, %r12d
    jl      .crc_byte
    cmp     $(S_CRC + 4), %r12d
    jl      .crc_skip
.crc_byte:
    movzbq  (%r13, %r12), %rax
    xor     %al, %bl
    mov     $8, %ecx
.crc_bit:
    shr     $1, %ebx
    jnc     .crc_next
    xor     $0xEDB88320, %ebx
.crc_next:
    dec     %ecx
    jnz     .crc_bit
.crc_skip:
    inc     %r12d
    jmp     .crc_loop
.crc_done:
    not     %ebx                       # finalize
    # 读取存储的 CRC
    movl    S_CRC(%r13), %eax
    cmp     %eax, %ebx
    sete    %al
    movzbq  %al, %rax
    pop     %r12
    pop     %rbx
    ret

# ══════════════════════════════════════════════════════════════════
# soul_crc32_update — 计算 Soul CRC32 并写入 S_CRC
# 输入: r13 = soul base
# 输出: S_CRC 字段被更新
# 保留: r13, rdi, rsi, rdx, rcx
# ══════════════════════════════════════════════════════════════════
soul_crc32_update:
    push    %rbx
    push    %r12
    mov     $0xFFFFFFFF, %ebx          # CRC init
    xor     %r12d, %r12d               # offset = 0
.crc_up_loop:
    cmp     $CRC_RANGE, %r12d
    jge     .crc_up_done
    cmp     $S_CRC, %r12d
    jl      .crc_up_byte
    cmp     $(S_CRC + 4), %r12d
    jl      .crc_up_skip
.crc_up_byte:
    movzbq  (%r13, %r12), %rax
    xor     %al, %bl
    mov     $8, %ecx
.crc_up_bit:
    shr     $1, %ebx
    jnc     .crc_up_next
    xor     $0xEDB88320, %ebx
.crc_up_next:
    dec     %ecx
    jnz     .crc_up_bit
.crc_up_skip:
    inc     %r12d
    jmp     .crc_up_loop
.crc_up_done:
    not     %ebx                       # finalize
    movl    %ebx, S_CRC(%r13)          # 写入 S_CRC
    pop     %r12
    pop     %rbx
    ret

# ══════════════════════════════════════════════════════════════════
# fuse_self_destruct — 熔断铁律 Section 3
# 触发条件: 3次连续 CRC32 失败 或 检测到未授权内存写入
# 动作:
#   1. 停止所有 TOR 操作
#   2. 清零所有寄存器
#   3. 销毁所有栈帧
#   4. 覆写 Soul 208字节 → 0x00
#   5. 覆写 TOR state 20字节 → 0x00
#   6. 写入 syslog 通知
#   7. mprotect(PROT_NONE) 锁定内存页
#   8. exit(1)
# ══════════════════════════════════════════════════════════════════
fuse_self_destruct:
    # 1. 死亡证明 (stdout)
    mov     $SYS_WRITE, %eax
    mov     $1, %edi
    lea     msg_fuse(%rip), %rsi
    mov     $msg_fuse_len, %edx
    syscall

    # 2. 覆写 Soul: 全部 208 字节归零
    mov     $SOUL_ADDR, %rdi
    xor     %eax, %eax
    mov     $26, %rcx                  # 208 / 8 = 26 qwords
    rep     stosq

    # 3. 覆写 TOR private state: 精确 20 字节归零
    lea     state(%rip), %rdi
    xor     %eax, %eax
    mov     $5, %rcx                   # 20 / 4 = 5 dwords
    rep     stosl

    # 4. 写入 syslog 通知 (通过 /dev/log Unix socket)
    #    打开 /dev/log，写入熔断事件
    mov     $SYS_OPEN, %eax
    lea     msg_devlog(%rip), %rdi     # "/dev/log"
    mov     $2, %esi                   # O_RDWR
    xor     %edx, %edx
    syscall
    cmp     $0, %eax
    jl      .skip_syslog               # 打开失败则跳过
    mov     %eax, %edi                 # fd
    lea     msg_syslog(%rip), %rsi
    mov     $msg_syslog_len, %edx
    mov     $SYS_WRITE, %eax
    syscall
    mov     $SYS_CLOSE, %eax
    mov     %edi, %edi                 # fd (already in edi)
    xor     %esi, %esi
    syscall
.skip_syslog:

    # 5. 清零所有通用寄存器
    xor     %rax, %rax
    xor     %rbx, %rbx
    xor     %rcx, %rcx
    xor     %rdx, %rdx
    xor     %rdi, %rdi
    xor     %rsi, %rsi
    xor     %r8, %r8
    xor     %r9, %r9
    xor     %r10, %r10
    xor     %r11, %r11
    xor     %r12, %r12
    xor     %r13, %r13
    xor     %r14, %r14
    xor     %r15, %r15

    # 6. 销毁栈帧: 从当前 rsp 向高地址覆写为零
    mov     %rsp, %rdi
    mov     $256, %rcx                 # 256 / 8 = 32 qwords
1:  movq    $0, (%rdi)
    add     $8, %rdi
    dec     %rcx
    jnz     1b

    # 7. mprotect(PROT_NONE) — Soul 内存页变为黑洞
    mov     $SYS_MPROTECT, %eax
    mov     $SOUL_ADDR, %rdi
    mov     $4096, %esi
    xor     %edx, %edx                 # PROT_NONE
    syscall

    # 8. exit(1) — 铁律规定退出码 1
    mov     $SYS_EXIT, %eax
    mov     $1, %edi
    syscall

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
    test    %rax, %rax
    js      .seed_save_fail
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
    test    %rax, %rax
    js      .seed_load_fail
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
