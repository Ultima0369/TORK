# Auto-generated from Makefile
# Section: ASM core

# ── 核心汇编编译规则 ──────────────────────────────────────────
#
# 🔒 汇编源码 (tork_core.asm, tork_soul.inc) 是专有软件。
# 源码位于 core-private/ 目录，不在公开仓库中。
#
# 编译方式（二选一）：
#   1. 本地有 core-private/ 目录：从 asm 源码编译
#   2. 本地无 core-private/：使用预编译的 src/core/tork_core.o
#
# 从 GitHub Releases 页面下载预编译 .o 文件：
#   https://github.com/Ultima0369/TORK/releases

ifeq ($(wildcard core-private/tork_core.asm),)
  # ── 模式 2: 使用预编译 .o ──────────────────────────────
  build/tork_core.o: src/core/tork_core.o
	cp src/core/tork_core.o build/tork_core.o
else
  # ── 模式 1: 从源码编译 ─────────────────────────────────
  build/tork_core.o: core-private/tork_core.asm core-private/tork_soul.inc
	mkdir -p build
	$(AS) -I core-private/ -o build/tork_core.o core-private/tork_core.asm
endif

build/tork_core: build/tork_core.o
	$(LD) -o build/tork_core build/tork_core.o
