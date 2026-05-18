/* ══════════════════════════════════════════════════════════════
 * test_engine.c — TORK 引擎单元测试
 *
 * 测试内容:
 *   - tork_context.h 结构体初始化
 *   - 模式切换
 *   - 核心 PID 管理
 *   - 本能评估结果存取
 * ══════════════════════════════════════════════════════════════ */

#include "unity.h"
#include "../src/engine/tork_context.h"
#include <string.h>

/* ── 测试组: Context 初始化 ── */

static void test_context_init_zero(void) {
    tork_context_t ctx;
    memset(&ctx, 0xFF, sizeof(ctx));  /* 填充 0xFF 模拟未初始化内存 */
    tork_context_init(&ctx);

    TEST_ASSERT_EQUAL_INT(0, (int)ctx.core_pid);
    TEST_ASSERT_EQUAL_INT(TORK_MODE_IDLE, (int)ctx.mode);
    TEST_ASSERT_EQUAL_UINT(0, (unsigned int)ctx.total_ticks);
    TEST_ASSERT_EQUAL_INT(0, ctx.last_instinct.drive);
    TEST_ASSERT_EQUAL_INT(0, ctx.last_instinct.survival);
    TEST_ASSERT_EQUAL_INT(0, ctx.last_instinct.curiosity);
    TEST_ASSERT_EQUAL_INT(0, (int)ctx.should_exit);
    TEST_ASSERT_EQUAL_INT(0, ctx.bitnet.enabled);
}

static void test_context_mode_transition(void) {
    tork_context_t ctx;
    tork_context_init(&ctx);

    ctx.mode = TORK_MODE_RUN;
    TEST_ASSERT_EQUAL_INT(TORK_MODE_RUN, (int)ctx.mode);

    ctx.mode = TORK_MODE_EVOLVE;
    TEST_ASSERT_EQUAL_INT(TORK_MODE_EVOLVE, (int)ctx.mode);

    ctx.mode = TORK_MODE_IDLE;
    TEST_ASSERT_EQUAL_INT(TORK_MODE_IDLE, (int)ctx.mode);

    ctx.mode = TORK_MODE_SLEEP;
    TEST_ASSERT_EQUAL_INT(TORK_MODE_SLEEP, (int)ctx.mode);
}

static void test_context_core_pid_management(void) {
    tork_context_t ctx;
    tork_context_init(&ctx);

    /* 设置 PID */
    ctx.core_pid = 12345;
    TEST_ASSERT_EQUAL_INT(12345, (int)ctx.core_pid);

    /* 清零 */
    ctx.core_pid = 0;
    TEST_ASSERT_EQUAL_INT(0, (int)ctx.core_pid);

    /* 负数 PID（错误情况） */
    ctx.core_pid = -1;
    TEST_ASSERT_EQUAL_INT(-1, (int)ctx.core_pid);
}

static void test_context_ticks_advance(void) {
    tork_context_t ctx;
    tork_context_init(&ctx);

    TEST_ASSERT_EQUAL_UINT(0, (unsigned int)ctx.total_ticks);
    ctx.total_ticks++;
    TEST_ASSERT_EQUAL_UINT(1, (unsigned int)ctx.total_ticks);
    ctx.total_ticks += 99;
    TEST_ASSERT_EQUAL_UINT(100, (unsigned int)ctx.total_ticks);
}

static void test_context_bitnet_bridge(void) {
    tork_context_t ctx;
    tork_context_init(&ctx);

    /* 默认禁用 */
    TEST_ASSERT_EQUAL_INT(0, ctx.bitnet.enabled);

    /* 启用 */
    ctx.bitnet.enabled = 1;
    ctx.bitnet.fd = 7;
    strcpy(ctx.bitnet.url, "http://127.0.0.1:8080");
    TEST_ASSERT_EQUAL_INT(1, ctx.bitnet.enabled);
    TEST_ASSERT_EQUAL_INT(7, ctx.bitnet.fd);
    TEST_ASSERT_EQUAL_STR("http://127.0.0.1:8080", ctx.bitnet.url);
}

static void test_context_should_exit_signal(void) {
    tork_context_t ctx;
    tork_context_init(&ctx);

    /* 默认不应退出 */
    TEST_ASSERT_FALSE(ctx.should_exit);

    /* 信号触发 */
    ctx.should_exit = 1;
    TEST_ASSERT_TRUE(ctx.should_exit);

    /* 再次清零 */
    ctx.should_exit = 0;
    TEST_ASSERT_FALSE(ctx.should_exit);
}

static void test_context_golden_flags(void) {
    tork_context_t ctx;
    tork_context_init(&ctx);

    TEST_ASSERT_EQUAL_INT(0, ctx.golden_exists);
    TEST_ASSERT_EQUAL_INT(0, ctx.golden_observe_remaining);
    TEST_ASSERT_EQUAL_INT(0, ctx.crc_fail_count);

    ctx.golden_exists = 1;
    ctx.golden_observe_remaining = 500;
    ctx.crc_fail_count = 3;

    TEST_ASSERT_EQUAL_INT(1, ctx.golden_exists);
    TEST_ASSERT_EQUAL_INT(500, ctx.golden_observe_remaining);
    TEST_ASSERT_EQUAL_INT(3, ctx.crc_fail_count);
}

/* ── 测试组运行器 ── */

int main(void) {
    unity_init();
    unity_begin("engine");

    RUN_TEST(context, init_zero);
    RUN_TEST(context, mode_transition);
    RUN_TEST(context, core_pid_management);
    RUN_TEST(context, ticks_advance);
    RUN_TEST(context, bitnet_bridge);
    RUN_TEST(context, should_exit_signal);
    RUN_TEST(context, golden_flags);

    unity_end_with_ret();
    return 0;
}
