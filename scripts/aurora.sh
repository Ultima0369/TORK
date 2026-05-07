#!/usr/bin/env bash
# TORK Aurora Observer 启动脚本
set -e
cd "$(dirname "$0")/.."

# 编译
if [ ! -f build/tork_engine ] || [ src/engine/torkd.c -nt build/tork_engine ]; then
    echo "Compiling..."
    make all
fi

# 启动引擎 (如果未运行)
if ! pgrep -x tork_engine > /dev/null 2>&1; then
    echo "Starting tork_engine..."
    ./build/tork_engine -q &
    ENGINE_PID=$!
    sleep 1
fi

# 启动极光观测器
echo "Starting Aurora Observer..."
python3 tork_aurora.py
