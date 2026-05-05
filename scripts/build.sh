#!/bin/bash
# TORK v1.0 — build script (fallback for environments without make)
set -e

BASE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$BASE_DIR"

mkdir -p build

echo ">>> Assembling tork_core"
as -I src/core/ -o build/tork_core.o src/core/tork_core.asm
ld -o build/tork_core build/tork_core.o

echo ">>> Compiling engine"
gcc -Wall -Wextra -O2 -Isrc/engine -Isrc/instinct -Isrc/code -Isrc/core \
    -c -o build/tork_engine.o src/engine/tork_engine.c
gcc -Wall -Wextra -O2 -Isrc/engine -Isrc/instinct -Isrc/code -Isrc/core \
    -c -o build/monitor.o src/engine/monitor.c

echo ">>> Compiling instinct"
gcc -Wall -Wextra -O2 -Isrc/engine -Isrc/instinct -Isrc/code -Isrc/core \
    -c -o build/instinct.o src/instinct/instinct.c

echo ">>> Compiling code"
gcc -Wall -Wextra -O2 -Isrc/engine -Isrc/instinct -Isrc/code -Isrc/core \
    -c -o build/code_reader.o src/code/code_reader.c
gcc -Wall -Wextra -O2 -Isrc/engine -Isrc/instinct -Isrc/code -Isrc/core \
    -c -o build/code_modifier.o src/code/code_modifier.c

echo ">>> Linking tork_engine"
gcc -o build/tork_engine \
    build/tork_engine.o build/monitor.o build/instinct.o \
    build/code_reader.o build/code_modifier.o -lm

echo ">>> Build complete: build/tork_core build/tork_engine"