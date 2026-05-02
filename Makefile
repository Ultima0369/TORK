# TORK v1.0 — Makefile
# One make to build them all, one make to link them.

AS  = as
LD  = ld
CC  = gcc
CFLAGS = -Wall -Wextra -O2 -Iengine -Iinstinct -Icode -Icore

.PHONY: all clean distclean run

all: build/tork_core build/tork_engine

# ── ASM core ────────────────────────────────────────────────────────

build/tork_core.o: core/tork_core.asm core/tork_soul.inc
	mkdir -p build
	$(AS) -I core/ -o build/tork_core.o core/tork_core.asm

build/tork_core: build/tork_core.o
	$(LD) -o build/tork_core build/tork_core.o

# ── C engine ────────────────────────────────────────────────────────

build/tork_engine.o: engine/tork_engine.c engine/soul_access.h engine/monitor.h engine/fission.h engine/blackboard.h engine/calibrator.h
	$(CC) $(CFLAGS) -c -o build/tork_engine.o engine/tork_engine.c

build/monitor.o: engine/monitor.c engine/monitor.h
	$(CC) $(CFLAGS) -c -o build/monitor.o engine/monitor.c

build/fission.o: engine/fission.c engine/fission.h engine/soul_access.h
	$(CC) $(CFLAGS) -c -o build/fission.o engine/fission.c

build/instinct.o: instinct/instinct.c instinct/instinct.h engine/calibrator.h
	$(CC) $(CFLAGS) -c -o build/instinct.o instinct/instinct.c

build/code_reader.o: code/code_reader.c code/code_reader.h
	$(CC) $(CFLAGS) -c -o build/code_reader.o code/code_reader.c

build/code_modifier.o: code/code_modifier.c code/code_modifier.h
	$(CC) $(CFLAGS) -c -o build/code_modifier.o code/code_modifier.c

build/blackboard.o: engine/blackboard.c engine/blackboard.h engine/soul_access.h
	$(CC) $(CFLAGS) -c -o build/blackboard.o engine/blackboard.c

build/calibrator.o: engine/calibrator.c engine/calibrator.h engine/blackboard.h
	$(CC) $(CFLAGS) -c -o build/calibrator.o engine/calibrator.c

build/tork_engine: build/tork_engine.o build/monitor.o build/instinct.o build/code_reader.o build/code_modifier.o build/fission.o build/blackboard.o build/calibrator.o
	$(CC) -o build/tork_engine build/tork_engine.o build/monitor.o build/instinct.o build/code_reader.o build/code_modifier.o build/fission.o build/blackboard.o build/calibrator.o -lm

# ── Targets ─────────────────────────────────────────────────────────

run: build/tork_engine build/tork_core
	./build/tork_engine 10

clean:
	rm -rf build/*.o build/tork_engine build/tork_core

distclean: clean
	rm -f soul.bin tork_engine.log
