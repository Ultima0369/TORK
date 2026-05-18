# TORK v3.15 — TLN: 三进制逻辑推理 + π-Heartbeat

AS  = as
LD  = ld
CC  = gcc
CFLAGS = -Wall -Wextra -O2 -Isrc/engine -Isrc/instinct -Isrc/code -Isrc/core -Isrc/install -Isrc/sandbox -Isrc/learning -Isrc/rollback

.PHONY: all clean distclean run install appimage grid torkd start stop status dashboard check-deps test sandbox

all: build/tork_core build/tork_engine build/tork_sandbox build/tork_sandbox_launcher build/tork_ask build/torkd_start build/tork build/tork_grid
	@mkdir -p build
	$(CC) $(CFLAGS) -o build/probe_env src/install/probe_env.c -lrt


# ── Modules (split from monolithic Makefile) ──────────────────
-include mk/core.mk
-include mk/sandbox.mk
-include mk/engine.mk
-include mk/learning.mk
-include mk/grid.mk
-include mk/misc.mk


# ── Tests ──────────────────────────────────────────────────────
TEST_CFLAGS = -Wall -Wextra -g -Isrc/engine -Isrc/instinct -Isrc/code -Isrc/core -Isrc/install -Isrc/sandbox -Isrc/learning -Isrc/rollback -Isrc/network -Itests

build/unity.o: tests/unity.c tests/unity.h
	$(CC) $(TEST_CFLAGS) -c -o $@ $<

# Engine tests
build/test_engine: tests/test_engine.c build/unity.o
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_engine.c build/unity.o

# Learning tests
build/test_learning: tests/test_learning.c build/unity.o
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_learning.c build/unity.o

# Core tests (existing)
build/test_core: tests/test_core.c build/unity.o
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_core.c build/unity.o -lm

# Run all tests
test: build/test_engine build/test_learning build/test_core
	@echo "=== Running all tests ==="
	@set -e; \
	for t in build/test_engine build/test_learning build/test_core; do \
		echo "--- $$t ---"; \
		./$$t; \
		echo ""; \
	done
	@echo "=== All tests passed ==="
