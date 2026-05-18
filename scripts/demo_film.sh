#!/usr/bin/env bash
# ──────────────────────────────────────────────────────
# TORK Film Observer — 演示脚本
#
# 3 分钟完整谱系:
#   0:00~0:45  极度平静 — 弱心搏, 无本能, 低学习
#   0:45~1:30  渐趋活跃 — 心搏增强, 本能频繁切换, 学习熵升
#   1:30~2:15  进化风暴 — 高压涌动, 胶片加速, 变异密集
#   2:15~3:00  回归平静 — 压力消退, 色彩渐暗, 缓流
#
# 用法: ./scripts/demo_film.sh
# ──────────────────────────────────────────────────────
set -e

BASE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OBSERVER="$BASE_DIR/film_observer.py"

# ── 检查依赖 ──────────────────────────────────────────
if ! python3 -c "import tkinter" 2>/dev/null; then
    echo "需要 python3-tk: sudo apt install python3-tk" >&2
    exit 1
fi

# ── 生成模拟数据 ───────────────────────────────────────
# 参数: $1=heartbeat $2=instinct_active $3=learning_entropy $4=evolution_pressure
emit() {
    printf '{"heartbeat":%.3f,"instinct_active":%.3f,"learning_entropy":%.3f,"evolution_pressure":%.3f}\n' \
        "$1" "$2" "$3" "$4"
    # 帧间隔由 film_observer 内部根据 evolution_pressure 控制
    # 这里用固定间隔发送, 观测器自行调速
    sleep 0.3
}

echo "TORK 胶片观测器 — 演示模式"
echo "  阶段: 平静 → 活跃 → 进化风暴 → 回归平静"
echo "  时长: ~3 分钟 | ESC 退出"

# ── 阶段 1: 极度平静 (0~45s) ──────────────────────────
echo "  [1/4] 极度平静..."
for i in $(seq 1 150); do
    # 微弱脉动: 0.02~0.08
    hb=$(python3 -c "print(f'{0.04 + 0.03 * __import__(\"math\").sin($i * 0.08):.3f}')")
    emit "$hb" 0.01 0.02 0.01
done

# ── 阶段 2: 渐趋活跃 (45~90s) ────────────────────────
echo "  [2/4] 渐趋活跃..."
for i in $(seq 1 150); do
    t=$i  # 1~150
    # 心搏: 0.05 → 0.7 (渐强)
    hb=$(python3 -c "print(f'{0.05 + 0.65 * ($t / 150.0):.3f}')")
    # 本能: 频繁切换 (fear/desire/curiosity 交替主导)
    inst=$(python3 -c "
import math
t = $t / 150.0
base = 0.05 + 0.75 * t
# 三驱力轮替
f = base * (0.5 + 0.5 * math.sin(t * 12))
d = base * (0.5 + 0.5 * math.sin(t * 12 + 2.094))
c = base * (0.5 + 0.5 * math.sin(t * 12 + 4.189))
print(f'{max(f, d, c):.3f}')
")
    # 学习熵: 0.02 → 0.6
    learn=$(python3 -c "print(f'{0.02 + 0.58 * ($t / 150.0):.3f}')")
    # 进化压力: 微弱上升
    evo=$(python3 -c "print(f'{0.01 + 0.15 * ($t / 150.0):.3f}')")
    emit "$hb" "$inst" "$learn" "$evo"
done

# ── 阶段 3: 进化风暴 (90~135s) ───────────────────────
echo "  [3/4] 进化风暴..."
for i in $(seq 1 150); do
    t=$i
    # 心搏: 高位震荡 0.6~0.95
    hb=$(python3 -c "print(f'{0.7 + 0.25 * __import__(\"math\").sin($t * 0.4):.3f}')")
    # 本能: 剧烈切换
    inst=$(python3 -c "
import math
t = $t / 150.0
f = 0.6 + 0.35 * math.sin(t * 30)
d = 0.6 + 0.35 * math.sin(t * 30 + 2.094)
c = 0.6 + 0.35 * math.sin(t * 30 + 4.189)
print(f'{max(f, d, c):.3f}')
")
    # 学习熵: 高位 0.5~0.9
    learn=$(python3 -c "print(f'{0.6 + 0.3 * __import__(\"math\").sin($t * 0.3):.3f}')")
    # 进化压力: 高! 0.7~1.0 (胶片加速!)
    evo=$(python3 -c "print(f'{0.75 + 0.25 * __import__(\"math\").sin($t * 0.2):.3f}')")
    emit "$hb" "$inst" "$learn" "$evo"
done

# ── 阶段 4: 回归平静 (135~180s) ──────────────────────
echo "  [4/4] 回归平静..."
for i in $(seq 1 150); do
    t=$i
    decay=$(python3 -c "print(f'{1.0 - ($t / 150.0):.3f}')")
    # 心搏: 0.7 → 0.05
    hb=$(python3 -c "print(f'{0.05 + 0.65 * $decay:.3f}')")
    # 本能: 0.8 → 0.02
    inst=$(python3 -c "print(f'{0.02 + 0.78 * $decay * (0.5 + 0.5 * __import__(\"math\").sin($t * 0.15)):.3f}')")
    # 学习熵: 0.7 → 0.03
    learn=$(python3 -c "print(f'{0.03 + 0.67 * $decay:.3f}')")
    # 进化压力: 0.8 → 0.01 (胶片减速)
    evo=$(python3 -c "print(f'{0.01 + 0.79 * $decay:.3f}')")
    emit "$hb" "$inst" "$learn" "$evo"
done

echo "演示结束"
