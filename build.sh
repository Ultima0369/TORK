#!/bin/bash
# TORK v1.0 — build script (fallback for environments without make)
set -e

mkdir -p build

echo ">>> Assembling tork_core"
as -I core/ -o build/tork_core.o core/tork_core.asm
ld -o build/tork_core build/tork_core.o

echo ">>> Compiling engine"
gcc -Wall -Wextra -O2 -Iengine -Iinstinct -Icode -Icore \
    -c -o build/tork_engine.o engine/tork_engine.c
gcc -Wall -Wextra -O2 -Iengine -Iinstinct -Icode -Icore \
    -c -o build/monitor.o engine/monitor.c

echo ">>> Compiling instinct"
gcc -Wall -Wextra -O2 -Iengine -Iinstinct -Icode -Icore \
    -c -o build/instinct.o instinct/instinct.c

echo ">>> Compiling code"
gcc -Wall -Wextra -O2 -Iengine -Iinstinct -Icode -Icore \
    -c -o build/code_reader.o code/code_reader.c
gcc -Wall -Wextra -O2 -Iengine -Iinstinct -Icode -Icore \
    -c -o build/code_modifier.o code/code_modifier.c

echo ">>> Linking tork_engine"
gcc -o build/tork_engine \
    build/tork_engine.o build/monitor.o build/instinct.o \
    build/code_reader.o build/code_modifier.o -lm

echo ">>> Build complete: build/tork_core build/tork_engine"
