	.file	"ref.c"
	.text
	.p2align 4
	.globl	memcpy_tork
	.type	memcpy_tork, @function
memcpy_tork:
.LFB0:
	.cfi_startproc
	movq	%rdi, %rax
	testq	%rdx, %rdx
	jz	.L2
	xorl	%ecx, %ecx
	.p2align 5
	.p2align 4
	.p2align 3
.L3:
	movzbl	(%rsi,%rcx), %r8d
	movb	%r8b, (%rax,%rcx)
	addq	$1, %rcx
	cmpq	%rcx, %rdx
	jne	.L3
.L2:
	ret
	.cfi_endproc
.LFE0:
	.size	memcpy_tork, .-memcpy_tork
	.ident	"GCC: (Debian 14.2.0-19) 14.2.0"
	.section	.note.GNU-stack,"",@progbits
