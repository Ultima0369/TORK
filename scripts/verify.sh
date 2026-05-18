#!/bin/bash
# TORK 系统健康验证 — 单命令确认"系统正常运行"
# 用法: make verify 或 bash scripts/verify.sh

set -euo pipefail

PASS=0
FAIL=0
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

header() { printf "\n  ── %s ──\n" "$1"; }
pass()   { PASS=$((PASS+1)); printf "  ${GREEN}✓${NC} %s\n" "$1"; }
fail()   { FAIL=$((FAIL+1)); printf "  ${RED}✗${NC} %s\n" "$1"; }
warn()   { printf "  ${YELLOW}⚠${NC} %s\n" "$1"; }

cd "$(dirname "$0")/.."
ROOT=$(pwd)

echo "════════════════════════════════════════════════════════"
echo "  TORK 系统健康验证"
echo "  $(date -Iseconds)"
echo "  工作目录: $ROOT"
echo "════════════════════════════════════════════════════════"

# ── 1. 环境依赖 ────────────────────────────────────────────────
header "1. 环境依赖"

if command -v as >/dev/null 2>&1; then
    pass "as (GNU assembler): $(as --version | head -1)"
else
    fail "as 未找到"
fi

if command -v gcc >/dev/null 2>&1; then
    pass "gcc: $(gcc --version | head -1)"
else
    fail "gcc 未找到"
fi

if command -v python3 >/dev/null 2>&1; then
    pyver=$(python3 --version)
    pass "python3: $pyver"
else
    fail "python3 未找到"
fi

if command -v make >/dev/null 2>&1; then
    pass "make: $(make --version 2>&1 | head -1)"
else
    fail "make 未找到"
fi

# 内核参数检查：ptrace_scope
if [ -f /proc/sys/kernel/yama/ptrace_scope ]; then
    scope=$(cat /proc/sys/kernel/yama/ptrace_scope)
    if [ "$scope" = "0" ]; then
        warn "ptrace_scope=0 (关闭) — 任意进程可 ptrace core"
    elif [ "$scope" = "1" ]; then
        pass "ptrace_scope=1 (默认) — 仅父进程可 ptrace"
    else
        warn "ptrace_scope=$scope — 限制严格，可能影响运行"
    fi
else
    warn "未检测到 ptrace_scope (可能为容器环境)"
fi

# ── 2. 全部编译（严格模式） ─────────────────────────────────────
header "2. 编译验证"

if make CFLAGS="-Wall -Wextra -Werror -O2 -Isrc/engine -Isrc/instinct -Isrc/code -Isrc/core -Isrc/install -Isrc/sandbox -Isrc/learning" all > /tmp/tork_build.log 2>&1; then
    pass "全部目标编译通过 (-Wall -Wextra -Werror -O2)"
else
    fail "编译失败 — 见 /tmp/tork_build.log"
    tail -20 /tmp/tork_build.log
fi

# ── 3. Soul 布局一致性校验 ─────────────────────────────────────
header "3. Soul 布局一致性 (C ↔ ASM)"

# 提取 ASM 端偏移
SOUL_INC="$ROOT/src/core/tork_soul.inc"
SOUL_H="$ROOT/src/engine/soul_access.h"

# 用 grep 提取所有 .equ 定义中的偏移并验证
MISMATCH=0
while IFS= read -r line; do
    if [[ $line =~ ^\.equ\ ([A-Z_]+),[[:space:]]*(0x[0-9A-Fa-f]+) ]]; then
        name="${BASH_REMATCH[1]}"
        asm_off="${BASH_REMATCH[2]}"
        # 转为大写十六进制用于对比
        asm_val=$(printf "%d" "$asm_off")
        # 在 C 头文件中查找同名宏
        c_line=$(grep -n "#define $name " "$SOUL_H" 2>/dev/null | head -1)
        if [ -n "$c_line" ]; then
            c_off=$(echo "$c_line" | grep -oP '0x[0-9A-Fa-f]+')
            c_val=$(printf "%d" "$c_off")
            if [ "$asm_val" != "$c_val" ]; then
                fail "$name: ASM=$asm_off C=$c_off 不一致"
                MISMATCH=$((MISMATCH+1))
            fi
        fi
    fi
done < <(grep '\.equ' "$SOUL_INC")

# 检查 SOUL_SIZE
asm_size=$(grep -oP 'SOUL_SIZE_BYTES,\s+\K\d+' "$SOUL_INC" | head -1)
c_size=$(grep -oP '#define\s+SOUL_SIZE\s+\K\d+' "$SOUL_H" | head -1)
if [ -n "$asm_size" ] && [ -n "$c_size" ]; then
    if [ "$asm_size" = "$c_size" ]; then
        pass "SOUL_SIZE: C/ASM 一致 ($asm_size 字节)"
    else
        fail "SOUL_SIZE: ASM=$asm_size C=$c_size 不一致"
        MISMATCH=$((MISMATCH+1))
    fi
fi

if [ "$MISMATCH" -eq 0 ]; then
    pass "所有 Soul 偏移量 C/ASM 一致"
fi

# ── 4. C 单元测试 ───────────────────────────────────────────────
header "4. C 单元测试"

if make test > /tmp/tork_ctest.log 2>&1; then
    ctest_result=$(tail -1 /tmp/tork_ctest.log)
    pass "C 单元测试通过: $ctest_result"
else
    fail "C 单元测试失败"
    tail -20 /tmp/tork_ctest.log
fi

# ── 5. Python 单元测试 ──────────────────────────────────────────
header "5. Python 单元测试 (进化引擎)"

if python3 tests/test_evolution.py > /tmp/tork_pytest.log 2>&1; then
    pytests=$(grep -oP '合计:\s+\K\d+' /tmp/tork_pytest.log | head -1)
    pass "Python 单元测试全部通过 ($pytests 项)"
else
    fail "Python 单元测试失败"
    tail -20 /tmp/tork_pytest.log
fi

# ── 6. 持久化文件完整性 ─────────────────────────────────────────
header "6. 持久化文件"

PERSIST_DIR="$ROOT/persist"
if [ -d "$PERSIST_DIR" ]; then
    # 检查关键持久化文件是否存在且非空
    for f in calibrator.bin experience.bin patterns.bin snapshots.bin \
             pi_index.bin self_build.bin watcher.bin baseline.bin \
             tune_params.bin mutation_guide.bin core_golden.sha256; do
        if [ -f "$PERSIST_DIR/$f" ]; then
            size=$(stat -c%s "$PERSIST_DIR/$f" 2>/dev/null || stat -f%z "$PERSIST_DIR/$f" 2>/dev/null || echo "?")
            if [ "$size" -gt 0 ] 2>/dev/null; then
                pass "$f ($size bytes)"
            else
                warn "$f (空文件)"
            fi
        else
            warn "$f (不存在 — 首次运行将自动创建)"
        fi
    done

    # 如果有 manifest.json，校验所有文件的 CRC
    if [ -f "$PERSIST_DIR/manifest.json" ]; then
        # 用 Python 解析 manifest 并校验每个文件
        python3 -c "
import json, os, struct, sys
try:
    with open('$PERSIST_DIR/manifest.json') as f:
        manifest = json.load(f)
except:
    sys.exit(0)  # 首次运行无 manifest

persist = '$PERSIST_DIR'
errors = 0
for field in ['soul_crc', 'bb_crc', 'params_crc', 'rules_crc']:
    if field not in manifest:
        continue
    # 文件路径: soul_crc → soul.bin
    fname = field.replace('_crc', '') + '.bin'
    fpath = os.path.join(persist, fname)
    if not os.path.exists(fpath):
        continue
    expected = int(manifest[field], 16) if isinstance(manifest[field], str) else manifest[field]
    # 计算实际 CRC
    crc = 0xFFFFFFFF
    with open(fpath, 'rb') as f:
        for chunk in iter(lambda: f.read(8192), b''):
            for byte in chunk:
                crc ^= byte
                for _ in range(8):
                    crc = (crc >> 1) ^ (0xEDB88320 if (crc & 1) else 0)
    crc = ~crc & 0xFFFFFFFF
    if crc != expected:
        print(f'  \\u2717 {fname}: CRC不匹配 (实际0x{crc:08X}, 期望0x{expected:08X})')
        errors += 1
if errors == 0:
    print(f'  所有 CRC 匹配 manifest')
else:
    print(f'  {errors} 个文件 CRC 异常')
    sys.exit(1)
" 2>&1 | while IFS= read -r line; do
    if [[ "$line" == "  \\u2717"* ]]; then
        fail "${line#  }"
    elif [[ "$line" == "  所有"* ]]; then
        pass "$line"
    elif [[ "$line" == "  "*":"* ]]; then
        warn "${line#  }"
    fi
done
    else
        warn "manifest.json 不存在 — 首次运行将自动创建"
    fi

    # 事务完整性：检查 .commit_tick
    if [ -f "$PERSIST_DIR/.commit_tick" ]; then
        tick=$(cat "$PERSIST_DIR/.commit_tick")
        pass "事务标记存在 (.commit_tick tick=$tick)"
    else
        # 如果持久化文件存在但无提交标记，说明上次保存未完成
        if ls "$PERSIST_DIR"/*.bin 2>/dev/null | head -1 > /dev/null 2>&1; then
            warn "持久化文件存在但无 .commit_tick — 上次保存可能未完成（加载时回退备份）"
        fi
    fi
else
    warn "persist/ 目录不存在 — 首次运行将自动创建"
fi

# ── 7. 本能参数范围验证 ─────────────────────────────────────────
header "7. 本能参数范围"

python3 -c "
import json, sys
# 从 strategy_generator 读取参数范围
sys.path.insert(0, '.')
from cloud.strategy_generator import KNOWN_PARAMS

errors = 0
for name, meta in KNOWN_PARAMS.items():
    if meta.get('min') is not None and meta.get('max') is not None:
        if meta['min'] >= meta['max']:
            print(f'  ✗ {name}: min({meta[\"min\"]}) >= max({meta[\"max\"]})')
            errors += 1
if errors:
    print(f'  发现 {errors} 个参数范围错误')
    sys.exit(1)
else:
    print(f'  所有 {len(KNOWN_PARAMS)} 个已知参数范围有效')
" 2>&1 | while IFS= read -r line; do
    if [[ "$line" == "  ✗"* ]]; then
        fail "${line#  }"
    elif [[ "$line" == "  所有"* ]]; then
        pass "${line#  }"
    fi
done

# ── 8. 构建产物完整性 ───────────────────────────────────────────
header "8. 构建产物"

for bin in build/tork_core build/tork_engine build/tork_sandbox \
           build/tork_sandbox_launcher build/tork_grid build/probe_env; do
    if [ -f "$bin" ]; then
        size=$(stat -c%s "$bin" 2>/dev/null || stat -f%z "$bin" 2>/dev/null || echo "?")
        pass "$bin ($size bytes)"
    else
        warn "$bin (未构建)"
    fi
done

# ── 汇总 ────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════════════════════"
echo "  验证结果: $PASS 通过, $FAIL 失败"
if [ "$FAIL" -eq 0 ]; then
    echo "  >>> 全部通过 <<<"
else
    echo "  >>> $FAIL 项失败 <<<"
fi
echo "════════════════════════════════════════════════════════"
exit $FAIL
