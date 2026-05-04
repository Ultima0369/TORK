#!/bin/bash
# TORK ASM 自进化闭环
# 读自己 → 变异 → 汇编 → 验证 → 替换

set -e
cd /home/lg/0EGG

SRC="core/tork_core.asm"
BACKUP="${SRC}.bak"
LOG="asm_evolution.log"
MAX_ATTEMPTS=5

echo "=== TORK ASM 自进化 @ $(date) ===" | tee -a "$LOG"

for ((attempt=1; attempt<=MAX_ATTEMPTS; attempt++)); do
    echo "--- 第 $attempt 轮 ---" | tee -a "$LOG"
    
    # 备份
    cp "$SRC" "$BACKUP"
    ORIG_SIZE=$(wc -c < "$SRC")
    ORIG_INSNS=$(as -I core/ -o /tmp/tork_test.o "$SRC" 2>/dev/null && 
                 objdump -d /tmp/tork_test.o 2>/dev/null | grep -c '^[[:space:]]' || echo 0)
    
    # 随机变异
    STRATEGIES=(inc_add dec_sub xor_zero test_cmp remove_nop lea_fold)
    STRATEGY=${STRATEGIES[$RANDOM % ${#STRATEGIES[@]}]}
    ./tools/asm_mutate.sh "$SRC" "$STRATEGY" 2>/dev/null || true
    
    # 尝试汇编
    if as -I core/ -o /tmp/tork_test.o "$SRC" 2>/tmp/tork_asm_err.log; then
        # 汇编通过 → 链接测试
        if ld -o /tmp/tork_test /tmp/tork_test.o 2>/tmp/tork_ld_err.log; then
            # 链接通过 → 运行测试 (500ms)
            timeout 1 /tmp/tork_test 2>/dev/null | head -3 > /tmp/tork_run.log
            RUN_OK=$?
            if [ $RUN_OK -eq 0 ] || [ $RUN_OK -eq 124 ]; then
                NEW_SIZE=$(wc -c < "$SRC")
                NEW_INSNS=$(objdump -d /tmp/tork_test.o | grep -c '^[[:space:]]')
                SAVED=$((ORIG_INSNS - NEW_INSNS))
                echo "✅ 变异成功 | $STRATEGY | 指令: $ORIG_INSNS→$NEW_INSNS ($SAVED)" | tee -a "$LOG"
                rm -f "$BACKUP"
                exit 0
            fi
        fi
    fi
    
    # 失败 → 回滚
    echo "❌ 第 $attempt 轮失败 ($STRATEGY) → 回滚" | tee -a "$LOG"
    cp "$BACKUP" "$SRC"
done

echo "❌ 全部 $MAX_ATTEMPTS 轮失败，保留原始代码" | tee -a "$LOG"
exit 1
