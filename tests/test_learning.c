/* ══════════════════════════════════════════════════════════════
 * test_learning.c — TORK 学习模块单元测试
 *
 * 测试内容:
 *   - pattern 模块基础功能
 *   - 模式查询与预测
 *   - 成功代码模式记录
 * ══════════════════════════════════════════════════════════════ */

#include "unity.h"
#include "../src/learning/pattern.h"
#include <string.h>

static void test_pattern_init(void) {
    pat_init();
    int count = pat_count();
    /* 刚初始化时应该有 0 个模式 */
    TEST_ASSERT_EQUAL_INT(0, count);
    /* 学习轮数应为 0 */
    TEST_ASSERT_EQUAL_UINT(0, (unsigned int)pat_cycles());
}

static void test_pattern_query_empty(void) {
    pat_init();
    float confidence = 0.0f;
    int action = pat_query_best_action(0, 0, 0, &confidence);
    /* 空模式库应返回 -1 或 0，置信度接近 0 */
    TEST_ASSERT_IN_RANGE(-1, 0, action);
    TEST_ASSERT_TRUE(confidence >= 0.0f && confidence <= 0.1f);
}

static void test_pattern_predict_empty(void) {
    pat_init();
    float outcome = pat_predict_outcome(0, 0, 0, 1);
    /* 无数据时预测 outcome 应为 0 */
    TEST_ASSERT_EQUAL_INT(0, (int)outcome);
}

static void test_pattern_record_success(void) {
    pat_init();
    uint32_t id = pat_record_success(0xABCD, 1500, 1, "test pattern");
    /* 应返回非零 ID */
    TEST_ASSERT_TRUE(id > 0);
}

static void test_pattern_query_by_category_empty(void) {
    pat_init();
    pat_entry_t entries[5];
    int n = pat_query_by_category(1, 5, entries);
    /* 空库应返回 0 */
    TEST_ASSERT_EQUAL_INT(0, n);
}

static void test_pattern_top_survival_empty(void) {
    pat_init();
    pat_entry_t entries[5];
    int n = pat_query_top_survival(5, entries);
    TEST_ASSERT_EQUAL_INT(0, n);
}

static void test_pattern_cleanup(void) {
    pat_init();
    pat_cleanup();
    /* 清理后应该还能安全地再次初始化 */
    pat_init();
    TEST_ASSERT_EQUAL_INT(0, pat_count());
}

/* ── 测试组运行器 ── */

int main(void) {
    unity_init();
    unity_begin("learning");

    RUN_TEST(pattern, init);
    RUN_TEST(pattern, query_empty);
    RUN_TEST(pattern, predict_empty);
    RUN_TEST(pattern, record_success);
    RUN_TEST(pattern, query_by_category_empty);
    RUN_TEST(pattern, top_survival_empty);
    RUN_TEST(pattern, cleanup);

    unity_end_with_ret();
    return 0;
}
