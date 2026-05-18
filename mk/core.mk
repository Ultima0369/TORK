# Auto-generated from Makefile
# Section: ASM core

# ── ASM core ────────────────────────────────────────────────────────

build/tork_core.o: src/core/tork_core.asm src/core/tork_soul.inc
	mkdir -p build
	$(AS) -I src/core/ -o build/tork_core.o src/core/tork_core.asm

build/tork_core: build/tork_core.o
	$(LD) -o build/tork_core build/tork_core.o


