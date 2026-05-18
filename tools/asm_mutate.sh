#!/bin/bash
# TORK ASM 自修改引擎 — 从旧版移植
# 用法: ./tools/asm_mutate.sh <source.s> [strategy]
# 策略: inc_add|dec_sub|xor_zero|test_cmp|remove_nop|lea_fold|random

SRC="${1:-core/tork_core.asm}"
STRATEGY="${2:-random}"
BACKUP="${SRC}.bak"
TMP="${SRC}.tmp"
LOG="asm_mutation.log"

[ ! -f "$SRC" ] && echo "❌ $SRC 不存在" && exit 1
cp "$SRC" "$BACKUP"

# 选择策略
if [ "$STRATEGY" = "random" ]; then
    STRATEGIES=(inc_add dec_sub xor_zero test_cmp remove_nop lea_fold)
    STRATEGY=${STRATEGIES[$RANDOM % ${#STRATEGIES[@]}]}
fi

MUTATIONS=0
case "$STRATEGY" in
    inc_add)
        # inc → add $1  (AT&T syntax: incl → addl $1,)
        sed 's/incl\s\+\(%[a-z0-9]*\)/addl\t$1, \1/g' "$SRC" > "$TMP"
        MUTATIONS=$(grep -c 'addl.*\$1' "$TMP" 2>/dev/null || echo 0)
        ;;
    dec_sub)
        # decl → subl $1
        sed 's/decl\s\+\(%[a-z0-9]*\)/subl\t$1, \1/g' "$SRC" > "$TMP"
        MUTATIONS=$(grep -c 'subl.*\$1' "$TMP" 2>/dev/null || echo 0)
        ;;
    xor_zero)
        # xor %reg,%reg → mov $0,%reg (but only for non-rax)
        sed 's/xorl\s\+%eax,%eax/movl\t$0, %eax/g; s/xorq\s\+%rax,%rax/movq\t$0, %rax/g' "$SRC" > "$TMP"
        MUTATIONS=$(grep -c 'mov.*\$0.*%e[abc]x\|mov.*\$0.*%r[abcd]' "$TMP" 2>/dev/null || echo 0)
        ;;
    test_cmp)
        # test %reg,%reg → cmp $0,%reg
        sed 's/test\([bwlq]\)\s\+%r[abcd]x,%r[abcd]x/cmp\1\t$0, %\0/g; s/test\([bwlq]\)\s\+%e[abcd]x,%e[abcd]x/cmp\1\t$0, %\0/g' "$SRC" > "$TMP"
        MUTATIONS=1
        ;;
    remove_nop)
        # 删除 nop 指令
        grep -v '^[[:space:]]*nop' "$SRC" > "$TMP"
        MUTATIONS=$(grep -c 'nop' "$SRC" 2>/dev/null || echo 0)
        ;;
    lea_fold)
        # lea (%reg),%reg → mov %reg,%reg (identical)
        # lea 0(%reg),%reg → mov %reg,%reg
        sed 's/lea\([bwlq]\)\?\s\+0\?(%r[abcds]x),%r[abcds]x/mov\1\t%r&, %r&/g' "$SRC" > "$TMP"
        MUTATIONS=$(grep -c 'mov.*%r.*, %r' "$TMP" 2>/dev/null || echo 0)
        ;;
esac

mv "$TMP" "$SRC"
echo "$(date '+%H:%M:%S') strategy=$STRATEGY mutations=$MUTATIONS" >> "$LOG"
echo "✅ $STRATEGY: $MUTATIONS 处变异 → $SRC"
exit 0
