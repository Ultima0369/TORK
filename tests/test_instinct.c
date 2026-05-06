/*
 * TORK Instinct 模块单元测试
 * 覆盖: instinct_evaluate, instinct_apply_tune
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── 被测模块 ──────────────────────────────────────────────────── */
#include "../src/instinct/instinct.h"

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) static void test_##name(void)
#define RUN(name) do { printf("  %-40s", #name); test_##name(); g_pass++; printf("PASS\n"); } while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    %s != %s  (line %d)\n", #a, #b, __LINE__); \
        g_fail++; return; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { \
        printf("FAIL\n    %s  (line %d)\n", #x, __LINE__); \
        g_fail++; return; \
    } \
} while(0)

#define ASSERT_FNEAR(a, b, eps) do { \
    float _a = (a), _b = (b), _e = (eps); \
    if (fabsf(_a - _b) > _e) { \
        printf("FAIL\n    %s=%.6f != %s=%.6f (eps=%.6f, line %d)\n", \
               #a, _a, #b, _b, _e, __LINE__); \
        g_fail++; return; \
    } \
} while(0)

/* ── 辅助: 构造全零 instinct_input_t ─────────────────────────────── */
static instinct_input_t make_zero_input(void) {
    instinct_input_t in;
    memset(&in, 0, sizeof(in));
    /* 修正需要特殊默认值的字段 */
    in.energy_mode           = 1;    /* 1=正常模式 (0=performance, 2/3=throttle) */
    in.branch_fork_ticks_ago = -1;   /* -1=never forked */
    in.pattern_best_action   = -1;   /* -1=no pattern */
    in.energy_throttle       = 0.0f; /* 无节流 */
    in.pattern_confidence    = 0.0f;
    return in;
}

/* ══════════════════════════════════════════════════════════════════
 *  1. hw_stress=0 时 fear 应接近 0
 * ══════════════════════════════════════════════════════════════════ */
TEST(fear_zero_when_no_stress) {
    instinct_input_t in = make_zero_input();
    in.hw_stress = 0;
    tork_instinct_t inst = instinct_evaluate(&in);
    ASSERT_FNEAR(inst.fear, 0.0f, 0.001f);
}

/* ══════════════════════════════════════════════════════════════════
 *  2. hw_stress=3 时 fear 最大 (FEAR_HIGH * fw, fw=1.0 默认)
 * ══════════════════════════════════════════════════════════════════ */
TEST(fear_max_at_stress_3) {
    instinct_input_t in = make_zero_input();
    in.hw_stress = 3;
    tork_instinct_t inst = instinct_evaluate(&in);
    /* FEAR_HIGH = 1.0, fw = 1.0 → fear = 1.0 */
    ASSERT_FNEAR(inst.fear, 1.0f, 0.001f);
}

/* ══════════════════════════════════════════════════════════════════
 *  2b. hw_stress=2 → fear = FEAR_MED * fw = 0.6
 * ══════════════════════════════════════════════════════════════════ */
TEST(fear_med_at_stress_2) {
    instinct_input_t in = make_zero_input();
    in.hw_stress = 2;
    tork_instinct_t inst = instinct_evaluate(&in);
    ASSERT_FNEAR(inst.fear, 0.6f, 0.001f);
}

/* ══════════════════════════════════════════════════════════════════
 *  2c. hw_stress=1 → fear = FEAR_LOW * fw = 0.3
 * ══════════════════════════════════════════════════════════════════ */
TEST(fear_low_at_stress_1) {
    instinct_input_t in = make_zero_input();
    in.hw_stress = 1;
    tork_instinct_t inst = instinct_evaluate(&in);
    ASSERT_FNEAR(inst.fear, 0.3f, 0.001f);
}

/* ══════════════════════════════════════════════════════════════════
 *  3. code_mod_success=1 时 desire > 0
 * ══════════════════════════════════════════════════════════════════ */
TEST(desire_positive_on_mod_success) {
    instinct_input_t in = make_zero_input();
    in.code_mod_success = 1;
    tork_instinct_t inst = instinct_evaluate(&in);
    /* DESIRE_MOD = 0.8, dw = 1.0 → desire 基数 0.8
     * 但还有 code_mod_success==1 触发的 BONUS_CLOUD curiosity 和其他加成
     * desire 本身 = DESIRE_MOD * dw = 0.8 */
    ASSERT_TRUE(inst.desire > 0.0f);
    ASSERT_FNEAR(inst.desire, 0.8f, 0.001f);
}

/* ══════════════════════════════════════════════════════════════════
 *  3b. code_opt_saved>0 (但 code_mod_success!=1) → desire = DESIRE_OPT * dw
 * ══════════════════════════════════════════════════════════════════ */
TEST(desire_on_opt_saved) {
    instinct_input_t in = make_zero_input();
    in.code_mod_success = 0;
    in.code_opt_saved = 5;
    tork_instinct_t inst = instinct_evaluate(&in);
    /* DESIRE_OPT = 0.5 * dw = 0.5 */
    ASSERT_FNEAR(inst.desire, 0.5f, 0.001f);
}

/* ══════════════════════════════════════════════════════════════════
 *  3c. elapsed > expected → desire = DESIRE_SLOW * dw = 0.3
 * ══════════════════════════════════════════════════════════════════ */
TEST(desire_on_slow_elapsed) {
    instinct_input_t in = make_zero_input();
    in.code_mod_success = 0;
    in.code_opt_saved = 0;
    in.elapsed = 1000;
    in.expected = 500;
    tork_instinct_t inst = instinct_evaluate(&in);
    ASSERT_FNEAR(inst.desire, 0.3f, 0.001f);
}

/* ══════════════════════════════════════════════════════════════════
 *  4. code_insns/code_ctrl > 0 时 curiosity > 0
 * ══════════════════════════════════════════════════════════════════ */
TEST(curiosity_positive_with_code_metrics) {
    instinct_input_t in = make_zero_input();
    in.code_insns = 100;
    in.code_ctrl  = 20;
    tork_instinct_t inst = instinct_evaluate(&in);
    /* ratio = 20/100 = 0.2, curiosity = 0.2 * CURI_RATIO(0.5) * cw(1.0) = 0.1 */
    ASSERT_TRUE(inst.curiosity > 0.0f);
    ASSERT_FNEAR(inst.curiosity, 0.1f, 0.001f);
}

/* ══════════════════════════════════════════════════════════════════
 *  4b. code_nop_count > 0 增加 curiosity (CURI_NOP = 0.2)
 * ══════════════════════════════════════════════════════════════════ */
TEST(curiosity_nop_bonus) {
    instinct_input_t in = make_zero_input();
    in.code_nop_count = 3;
    tork_instinct_t inst = instinct_evaluate(&in);
    /* 只有 NOP bonus: CURI_NOP * cw = 0.2 */
    ASSERT_FNEAR(inst.curiosity, 0.2f, 0.001f);
}

/* ══════════════════════════════════════════════════════════════════
 *  5. drive = desire - fear + curiosity 方向正确
 *     场景: high desire, low fear, moderate curiosity → drive > 0
 * ══════════════════════════════════════════════════════════════════ */
TEST(drive_direction_positive) {
    instinct_input_t in = make_zero_input();
    in.hw_stress = 0;            /* fear = 0 */
    in.code_mod_success = 1;     /* desire = 0.8 */
    in.code_insns = 100;         /* curiosity = ratio * 0.5 */
    in.code_ctrl  = 30;
    tork_instinct_t inst = instinct_evaluate(&in);

    float drive = inst.desire - inst.fear + inst.curiosity;
    ASSERT_TRUE(drive > 0.0f);
    /* desire > fear, curiosity > 0, 所以 drive 应该接近 desire + curiosity */
}

/* ══════════════════════════════════════════════════════════════════
 *  5b. drive 方向: high fear, zero desire → drive < 0
 * ══════════════════════════════════════════════════════════════════ */
TEST(drive_direction_negative) {
    instinct_input_t in = make_zero_input();
    in.hw_stress = 3;            /* fear = 1.0 */
    in.code_mod_success = 0;     /* desire = 0 */
    in.code_insns = 0;           /* curiosity = 0 */
    in.code_ctrl  = 0;
    tork_instinct_t inst = instinct_evaluate(&in);

    float drive = inst.desire - inst.fear + inst.curiosity;
    ASSERT_TRUE(drive < 0.0f);
}

/* ══════════════════════════════════════════════════════════════════
 *  5c. drive 方向: fear 和 desire 平衡 → drive 约等于 curiosity
 * ══════════════════════════════════════════════════════════════════ */
TEST(drive_direction_balanced) {
    instinct_input_t in = make_zero_input();
    in.hw_stress = 1;            /* fear = 0.3 */
    in.code_mod_success = 1;     /* desire = 0.8 */
    in.code_insns = 100;
    in.code_ctrl  = 20;          /* curiosity ≈ 0.1 */
    tork_instinct_t inst = instinct_evaluate(&in);

    float drive = inst.desire - inst.fear + inst.curiosity;
    /* desire - fear ≈ 0.5, curiosity ≈ 0.1+, drive ≈ 0.6+ */
    ASSERT_TRUE(drive > 0.0f);
}

/* ══════════════════════════════════════════════════════════════════
 *  6. 边界: 全零输入不崩溃
 * ══════════════════════════════════════════════════════════════════ */
TEST(no_crash_on_zero_input) {
    instinct_input_t in = make_zero_input();
    tork_instinct_t inst = instinct_evaluate(&in);
    /* 只要不崩溃就算通过; 验证返回值在合理范围 */
    (void)inst;
}

/* ══════════════════════════════════════════════════════════════════
 *  6b. 全零输入: fear=0, desire=0, curiosity=0
 * ══════════════════════════════════════════════════════════════════ */
TEST(all_zero_produces_zero_instinct) {
    instinct_input_t in = make_zero_input();
    tork_instinct_t inst = instinct_evaluate(&in);
    ASSERT_FNEAR(inst.fear, 0.0f, 0.001f);
    ASSERT_FNEAR(inst.desire, 0.0f, 0.001f);
    ASSERT_FNEAR(inst.curiosity, 0.0f, 0.001f);
}

/* ══════════════════════════════════════════════════════════════════
 *  7. instinct_apply_tune 不崩溃
 * ══════════════════════════════════════════════════════════════════ */
TEST(apply_tune_no_crash) {
    /* instinct_apply_tune 调用 tune_get_params()，
     * 链接时需要 self_tune.o 或 stub 提供符号。
     * 此处验证函数可调用不崩溃。 */
    instinct_apply_tune();
}

/* ══════════════════════════════════════════════════════════════════
 *  补充: restored_files > 0 时 fear 被衰减 (MULT_RESTORE_FEAR=0.9)
 * ══════════════════════════════════════════════════════════════════ */
TEST(fear_reduced_by_restore) {
    instinct_input_t in_base = make_zero_input();
    in_base.hw_stress = 3;
    tork_instinct_t base = instinct_evaluate(&in_base);

    instinct_input_t in_rest = make_zero_input();
    in_rest.hw_stress = 3;
    in_rest.restored_files = 1;
    tork_instinct_t rest = instinct_evaluate(&in_rest);

    ASSERT_TRUE(rest.fear < base.fear);
    ASSERT_FNEAR(rest.fear, base.fear * 0.9f, 0.001f);
}

/* ══════════════════════════════════════════════════════════════════
 *  补充: wins > 0 增加 desire (BONUS_WIN=0.3)
 * ══════════════════════════════════════════════════════════════════ */
TEST(desire_boosted_by_wins) {
    instinct_input_t in = make_zero_input();
    in.wins = 5;
    tork_instinct_t inst = instinct_evaluate(&in);
    /* 无 code_mod_success/opt_saved/elapsed, desire 只有 BONUS_WIN = 0.3 */
    ASSERT_FNEAR(inst.desire, 0.3f, 0.001f);
}

/* ══════════════════════════════════════════════════════════════════
 *  补充: active_rules > 0 增加 curiosity (BONUS_RULE=0.3)
 * ══════════════════════════════════════════════════════════════════ */
TEST(curiosity_boosted_by_rules) {
    instinct_input_t in = make_zero_input();
    in.active_rules = 3;
    tork_instinct_t inst = instinct_evaluate(&in);
    ASSERT_TRUE(inst.curiosity > 0.0f);
    /* BONUS_RULE = 0.3, 无其他 curiosity 来源 */
    ASSERT_FNEAR(inst.curiosity, 0.3f, 0.001f);
}

/* ══════════════════════════════════════════════════════════════════
 *  补充: energy_mode=0 (performance) 放大 curiosity 和 desire
 * ══════════════════════════════════════════════════════════════════ */
TEST(perf_mode_amplifies_curiosity_desire) {
    instinct_input_t in_normal = make_zero_input();
    in_normal.code_mod_success = 1;   /* desire 基数 */
    in_normal.code_insns = 100;
    in_normal.code_ctrl  = 20;
    tork_instinct_t normal = instinct_evaluate(&in_normal);

    instinct_input_t in_perf = make_zero_input();
    in_perf.code_mod_success = 1;
    in_perf.code_insns = 100;
    in_perf.code_ctrl  = 20;
    in_perf.energy_mode = 0;          /* performance */
    tork_instinct_t perf = instinct_evaluate(&in_perf);

    ASSERT_TRUE(perf.curiosity > normal.curiosity);
    ASSERT_TRUE(perf.desire > normal.desire);
}

/* ══════════════════════════════════════════════════════════════════
 *  补充: energy_mode=2 (throttle) 缩减 curiosity 和 desire，增加 fear
 * ══════════════════════════════════════════════════════════════════ */
TEST(throttle_mode_reduces_curiosity_desire_increases_fear) {
    instinct_input_t in_base = make_zero_input();
    in_base.hw_stress = 1;           /* fear = 0.3 */
    in_base.code_mod_success = 1;    /* desire */
    in_base.code_insns = 100;
    in_base.code_ctrl  = 20;
    tork_instinct_t base = instinct_evaluate(&in_base);

    instinct_input_t in_thr = make_zero_input();
    in_thr.hw_stress = 1;
    in_thr.code_mod_success = 1;
    in_thr.code_insns = 100;
    in_thr.code_ctrl  = 20;
    in_thr.energy_mode = 2;          /* throttled */
    tork_instinct_t thr = instinct_evaluate(&in_thr);

    ASSERT_TRUE(thr.curiosity < base.curiosity);
    ASSERT_TRUE(thr.desire < base.desire);
    ASSERT_TRUE(thr.fear > base.fear);
}

/* ══════════════════════════════════════════════════════════════════
 *  补充: env_changed 增加好奇和欲望，减少恐惧
 * ══════════════════════════════════════════════════════════════════ */
TEST(env_changed_boosts_curiosity_reduces_fear) {
    instinct_input_t in_base = make_zero_input();
    in_base.hw_stress = 2;           /* fear = 0.6 */
    in_base.code_mod_success = 1;    /* desire */
    tork_instinct_t base = instinct_evaluate(&in_base);

    instinct_input_t in_env = make_zero_input();
    in_env.hw_stress = 2;
    in_env.code_mod_success = 1;
    in_env.env_changed = 1;
    tork_instinct_t env = instinct_evaluate(&in_env);

    ASSERT_TRUE(env.curiosity > base.curiosity);
    ASSERT_TRUE(env.fear < base.fear);
    ASSERT_TRUE(env.desire > base.desire);
}

/* ══════════════════════════════════════════════════════════════════
 *  补充: idle_discoveries > 0 增加 curiosity，减少 desire
 * ══════════════════════════════════════════════════════════════════ */
TEST(idle_discoveries_boosts_curiosity_penalizes_desire) {
    instinct_input_t in = make_zero_input();
    in.idle_discoveries = 2;
    tork_instinct_t inst = instinct_evaluate(&in);
    /* BONUS_IDLE_DISC = 0.2 加到 curiosity, PENAL_IDLE_REST = 0.05 从 desire 减 */
    ASSERT_TRUE(inst.curiosity > 0.0f);
    ASSERT_TRUE(inst.desire < 0.0f);
}

/* ══════════════════════════════════════════════════════════════════
 *  补充: NULL params 指针使用 fallback_params，不崩溃
 * ══════════════════════════════════════════════════════════════════ */
TEST(null_params_uses_fallback) {
    instinct_input_t in = make_zero_input();
    in.params = NULL;
    in.hw_stress = 3;   /* 触发 fear 计算 */
    tork_instinct_t inst = instinct_evaluate(&in);
    /* fallback weights 都是 100 → fw=1.0, fear = FEAR_HIGH = 1.0 */
    ASSERT_FNEAR(inst.fear, 1.0f, 0.001f);
}

/* ══════════════════════════════════════════════════════════════════
 *  补充: instinct_print 不崩溃
 * ══════════════════════════════════════════════════════════════════ */
TEST(print_no_crash) {
    tork_instinct_t inst = {0.5f, 0.3f, 0.8f};
    /* 重定向 stdout 到 /dev/null 避免噪音 */
    FILE *devnull = fopen("/dev/null", "w");
    /* instinct_print 输出到 stdout, 直接调用即可——测试不崩溃 */
    instinct_print(1, 42, &inst);
    (void)devnull;
}

/* ══════════════════════════════════════════════════════════════════
 *  主函数
 * ══════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("TORK Instinct Unit Tests\n");
    printf("════════════════════════════════════════════════════════\n\n");

    printf("[Fear]\n");
    RUN(fear_zero_when_no_stress);
    RUN(fear_max_at_stress_3);
    RUN(fear_med_at_stress_2);
    RUN(fear_low_at_stress_1);

    printf("\n[Desire]\n");
    RUN(desire_positive_on_mod_success);
    RUN(desire_on_opt_saved);
    RUN(desire_on_slow_elapsed);
    RUN(desire_boosted_by_wins);

    printf("\n[Curiosity]\n");
    RUN(curiosity_positive_with_code_metrics);
    RUN(curiosity_nop_bonus);
    RUN(curiosity_boosted_by_rules);

    printf("\n[Drive Direction]\n");
    RUN(drive_direction_positive);
    RUN(drive_direction_negative);
    RUN(drive_direction_balanced);

    printf("\n[Boundary]\n");
    RUN(no_crash_on_zero_input);
    RUN(all_zero_produces_zero_instinct);
    RUN(null_params_uses_fallback);

    printf("\n[Apply Tune]\n");
    RUN(apply_tune_no_crash);

    printf("\n[Misc Interactions]\n");
    RUN(fear_reduced_by_restore);
    RUN(perf_mode_amplifies_curiosity_desire);
    RUN(throttle_mode_reduces_curiosity_desire_increases_fear);
    RUN(env_changed_boosts_curiosity_reduces_fear);
    RUN(idle_discoveries_boosts_curiosity_penalizes_desire);
    RUN(print_no_crash);

    printf("\n════════════════════════════════════════════════════════\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);

    return g_fail > 0 ? 1 : 0;
}
