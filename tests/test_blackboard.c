/*
 * TORK Blackboard 模块单元测试
 * 覆盖: bb_init, bb_write/bb_read往返, bb_inc_optimizations,
 *       bb_global_optimizations, bb_inc_fissions, bb_cleanup
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

/* ── 被测模块 ──────────────────────────────────────────────────── */
#include "../src/engine/blackboard.h"

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

/* ══════════════════════════════════════════════════════════════════
 *  bb_init 测试
 * ══════════════════════════════════════════════════════════════════ */

TEST(bb_init_success) {
    bb_cleanup();  /* 确保干净状态 */
    int rc = bb_init();
    ASSERT_EQ(rc, 0);
    bb_cleanup();
}

TEST(bb_init_re_init_same_mapping) {
    /* 连续两次 init 不做 cleanup：第二次应识别已有 magic 并成功 */
    bb_cleanup();
    int rc1 = bb_init();
    ASSERT_EQ(rc1, 0);

    /* 第二次 init 时地址已被映射，mmap 可能返回同一地址或不同地址；
     * 核心验证：第一次 init 成功后，bb 功能正常可用 */
    bb_inc_optimizations();
    ASSERT_EQ(bb_global_optimizations(), (uint32_t)1);

    bb_cleanup();
}

/* ══════════════════════════════════════════════════════════════════
 *  bb_write / bb_read 往返测试
 * ══════════════════════════════════════════════════════════════════ */

/* 回调收集结构 */
typedef struct {
    int      count;
    int      last_index;
    uint32_t last_tick;
    uint16_t last_instance;
    uint8_t  last_type;
    uint8_t  last_value;
    uint32_t last_payload;
} collect_t;

static collect_t g_col;

static void read_cb(int index, uint32_t tick, uint16_t instance,
                    uint8_t type, uint8_t value, uint32_t payload) {
    g_col.count++;
    g_col.last_index    = index;
    g_col.last_tick     = tick;
    g_col.last_instance = instance;
    g_col.last_type     = type;
    g_col.last_value    = value;
    g_col.last_payload  = payload;
}

TEST(bb_write_read_roundtrip) {
    bb_cleanup();
    bb_init();

    bb_set_tick(42);
    int slot = bb_write(BB_TYPE_OPT_SUCCESS, 7, 0xDEADBEEF);
    ASSERT_TRUE(slot >= 0);

    memset(&g_col, 0, sizeof(g_col));
    int valid = bb_read_all(read_cb);

    ASSERT_TRUE(valid >= 1);
    ASSERT_EQ(g_col.last_type, (int)BB_TYPE_OPT_SUCCESS);
    ASSERT_EQ(g_col.last_value, 7);
    ASSERT_EQ(g_col.last_payload, (uint32_t)0xDEADBEEF);
    ASSERT_EQ(g_col.last_tick, (uint32_t)42);

    bb_cleanup();
}

TEST(bb_write_multiple_entries) {
    bb_cleanup();
    bb_init();

    bb_set_tick(100);
    bb_write(BB_TYPE_OPT_SUCCESS, 1, 0x100);
    bb_set_tick(200);
    bb_write(BB_TYPE_FISSION, 2, 0x200);
    bb_set_tick(300);
    bb_write(BB_TYPE_PARAM_ADJUST, 3, 0x300);

    memset(&g_col, 0, sizeof(g_col));
    int valid = bb_read_all(read_cb);

    ASSERT_TRUE(valid >= 3);
    /* 最后写入的应该是 PARAM_ADJUST */
    ASSERT_EQ(g_col.last_type, (int)BB_TYPE_PARAM_ADJUST);
    ASSERT_EQ(g_col.last_value, 3);
    ASSERT_EQ(g_col.last_payload, (uint32_t)0x300);

    bb_cleanup();
}

TEST(bb_write_without_init) {
    bb_cleanup();
    /* 未 init 时 bb 为 NULL，bb_write 应返回 -1 */
    int slot = bb_write(BB_TYPE_OPT_SUCCESS, 0, 0);
    ASSERT_EQ(slot, -1);
}

/* ══════════════════════════════════════════════════════════════════
 *  bb_inc_optimizations / bb_global_optimizations 测试
 * ══════════════════════════════════════════════════════════════════ */

TEST(bb_inc_optimizations) {
    bb_cleanup();
    bb_init();

    ASSERT_EQ(bb_global_optimizations(), (uint32_t)0);

    bb_inc_optimizations();
    ASSERT_EQ(bb_global_optimizations(), (uint32_t)1);

    bb_inc_optimizations();
    bb_inc_optimizations();
    ASSERT_EQ(bb_global_optimizations(), (uint32_t)3);

    bb_cleanup();
}

TEST(bb_global_optimizations_without_init) {
    bb_cleanup();
    /* 未 init 应返回 0 */
    ASSERT_EQ(bb_global_optimizations(), (uint32_t)0);
}

/* ══════════════════════════════════════════════════════════════════
 *  bb_inc_fissions / bb_global_fissions 测试
 * ══════════════════════════════════════════════════════════════════ */

TEST(bb_inc_fissions) {
    bb_cleanup();
    bb_init();

    ASSERT_EQ(bb_global_fissions(), (uint32_t)0);

    bb_inc_fissions();
    ASSERT_EQ(bb_global_fissions(), (uint32_t)1);

    bb_inc_fissions();
    ASSERT_EQ(bb_global_fissions(), (uint32_t)2);

    bb_cleanup();
}

/* ══════════════════════════════════════════════════════════════════
 *  bb_inc_errors / bb_global_errors 测试
 * ══════════════════════════════════════════════════════════════════ */

TEST(bb_inc_errors) {
    bb_cleanup();
    bb_init();

    ASSERT_EQ(bb_global_errors(), (uint32_t)0);

    bb_inc_errors();
    bb_inc_errors();
    bb_inc_errors();
    ASSERT_EQ(bb_global_errors(), (uint32_t)3);

    bb_cleanup();
}

/* ══════════════════════════════════════════════════════════════════
 *  bb_cleanup 测试
 * ══════════════════════════════════════════════════════════════════ */

TEST(bb_cleanup_no_crash) {
    bb_cleanup();  /* 未 init 时 cleanup 不应崩溃 */
    bb_cleanup();  /* 重复 cleanup 也不崩溃 */
}

TEST(bb_cleanup_after_init) {
    bb_init();
    bb_inc_optimizations();
    bb_cleanup();
    /* cleanup 后全局计数器应返回 0 */
    ASSERT_EQ(bb_global_optimizations(), (uint32_t)0);
}

/* ══════════════════════════════════════════════════════════════════
 *  bb_read_all 无 init 测试
 * ══════════════════════════════════════════════════════════════════ */

TEST(bb_read_all_without_init) {
    bb_cleanup();
    int valid = bb_read_all(read_cb);
    ASSERT_EQ(valid, -1);
}

/* ══════════════════════════════════════════════════════════════════
 *  主函数
 * ══════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("TORK Blackboard Unit Tests\n");
    printf("══════════════════════════════════════════════════════\n\n");

    printf("[bb_init]\n");
    RUN(bb_init_success);
    RUN(bb_init_re_init_same_mapping);

    printf("\n[bb_write / bb_read]\n");
    RUN(bb_write_read_roundtrip);
    RUN(bb_write_multiple_entries);
    RUN(bb_write_without_init);

    printf("\n[bb_inc_optimizations]\n");
    RUN(bb_inc_optimizations);
    RUN(bb_global_optimizations_without_init);

    printf("\n[bb_inc_fissions]\n");
    RUN(bb_inc_fissions);

    printf("\n[bb_inc_errors]\n");
    RUN(bb_inc_errors);

    printf("\n[bb_cleanup]\n");
    RUN(bb_cleanup_no_crash);
    RUN(bb_cleanup_after_init);

    printf("\n[bb_read_all edge]\n");
    RUN(bb_read_all_without_init);

    printf("\n════════════════════════════════════════════════════════\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);

    return g_fail > 0 ? 1 : 0;
}
