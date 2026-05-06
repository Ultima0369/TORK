#!/usr/bin/env bash
# ──────────────────────────────────────────────────────
# TORK Film Observer — 胶片观测器启动脚本
#
# 数据流: tork_engine → torkd (Unix Socket) → state 命令
#         → JSON 转换 → stdin → film_observer.py
#
# 用法:
#   ./scripts/tork_observer.sh          # 启动引擎 + 观测器
#   ./scripts/tork_observer.sh --no-engine  # 仅启动观测器 (引擎已运行)
# ──────────────────────────────────────────────────────
set -e

BASE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="$BASE_DIR/build/tork_engine"
OBSERVER="$BASE_DIR/film_observer.py"
SOCKET="/tmp/torkd.sock"
POLL_INTERVAL=0.5  # 秒

# ── 检查依赖 ──────────────────────────────────────────
check_deps() {
    if ! command -v python3 &>/dev/null; then
        echo "需要 python3" >&2; exit 1
    fi
    if ! python3 -c "import tkinter" 2>/dev/null; then
        echo "需要 python3-tk: sudo apt install python3-tk" >&2; exit 1
    fi
    if ! command -v socat &>/dev/null; then
        echo "需要 socat: sudo apt install socat" >&2; exit 1
    fi
}

# ── 启动引擎 (如果未运行) ──────────────────────────────
start_engine() {
    if pgrep -x tork_engine &>/dev/null; then
        echo "引擎已运行 (PID: $(pgrep -x tork_engine))"
        return
    fi
    if [ ! -f "$ENGINE" ]; then
        echo "编译引擎..."
        make -C "$BASE_DIR" all
    fi
    echo "启动引擎..."
    "$ENGINE" -q &
    ENGINE_PID=$!
    # 等待 socket 就绪
    for i in $(seq 1 10); do
        if [ -S "$SOCKET" ]; then
            echo "引擎就绪 (PID: $ENGINE_PID)"
            return
        fi
        sleep 0.5
    done
    echo "引擎启动超时" >&2; exit 1
}

# ── 轮询 torkd state 并转换为 film_observer 格式 ──────
# torkd state 返回:
#   {"heartbeat":{tick,hw_stress,heartbeat_ms,drive},
#    "instinct":{fear,desire,curiosity},
#    "learning":{tln_action,tln_modify,tln_explore,tln_energy,...},
#    "evolution":{gen_count,mutation_count,...}}
#
# film_observer 期望:
#   {"heartbeat":0~1, "instinct_active":0~1,
#    "learning_entropy":0~1, "evolution_pressure":0~1}
poll_state() {
    while true; do
        # 通过 socat 查询 torkd
        raw=$(echo "state" | socat - UNIX-CONNECT:"$SOCKET" 2>/dev/null) || true

        if [ -n "$raw" ]; then
            # 用 python3 做转换 (比 shell 解析 JSON 可靠)
            python3 -c "
import json, sys
try:
    d = json.loads('''$raw''')
    hb = d.get('heartbeat', {})
    inst = d.get('instinct', {})
    learn = d.get('learning', {})
    evo = d.get('evolution', {})

    # heartbeat: drive 绝对值归一化 (0~128 → 0~1)
    drive = hb.get('drive', 0)
    heartbeat = min(1.0, abs(drive) / 128.0)

    # instinct_active: 三驱力最大值
    fear = inst.get('fear', 0.0)
    desire = inst.get('desire', 0.0)
    curiosity = inst.get('curiosity', 0.0)
    instinct_active = max(fear, desire, curiosity)

    # learning_entropy: TLN 活性 + 经验密度
    tln = (abs(learn.get('tln_action', 0)) +
           abs(learn.get('tln_modify', 0)) +
           abs(learn.get('tln_explore', 0)) +
           abs(learn.get('tln_energy', 0))) / 4.0
    exp_count = learn.get('experience_count', 0)
    exp_norm = min(1.0, exp_count / 5000.0)
    learning_entropy = min(1.0, 0.6 * tln + 0.4 * exp_norm)

    # evolution_pressure: 变异率 + 世代密度
    gen = evo.get('gen_count', 0)
    mut = evo.get('mutation_count', 0)
    mut_rate = (mut / gen) if gen > 0 else 0.0
    evolution_pressure = min(1.0, mut_rate / 5.0)

    print(json.dumps({
        'heartbeat': round(heartbeat, 3),
        'instinct_active': round(instinct_active, 3),
        'learning_entropy': round(learning_entropy, 3),
        'evolution_pressure': round(evolution_pressure, 3)
    }))
except Exception:
    pass
" 2>/dev/null || true
        fi

        sleep "$POLL_INTERVAL"
    done
}

# ── 主流程 ──────────────────────────────────────────────
check_deps

if [ "$1" != "--no-engine" ]; then
    start_engine
fi

echo "启动胶片观测器..."
echo "  ESC 退出 | 数据来自 torkd state"

# 轮询进程的 PID (用于清理)
POLL_PID=""

cleanup() {
    [ -n "$POLL_PID" ] && kill "$POLL_PID" 2>/dev/null || true
    # 如果我们启动了引擎, 不自动关闭 (用户可能还想用)
    exit 0
}
trap cleanup EXIT INT TERM

# 启动轮询 → 管道 → 观测器
poll_state | python3 "$OBSERVER" &
POLL_PID=$(jobs -p | tail -1)

wait
