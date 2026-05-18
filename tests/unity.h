/* ══════════════════════════════════════════════════════════════
 * Unity — C 单元测试框架 (TORK 嵌入版)
 *
 * Unity 是嵌入式 C 项目最广泛使用的测试框架。
 * 整个框架一个 .h + .c, 约 600 行, 零依赖。
 *
 * 许可证: MIT (兼容 GPLv3)
 * 原始项目: https://github.com/ThrowTheSwitch/Unity
 * ══════════════════════════════════════════════════════════════ */

#ifndef TORK_UNITY_H
#define TORK_UNITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* ── 测试状态 ──────────────────────────────────────────── */
typedef struct {
    uint32_t total_tests;
    uint32_t passed;
    uint32_t failed;
    uint32_t ignored;
    const char *current_test;
    const char *current_file;
    int         current_line;
} unity_state_t;

extern unity_state_t Unity;

/* ── 初始化和报告 ──────────────────────────────────────── */
void unity_init(void);
void unity_begin(const char *name);
void unity_end(void);
void unity_end_with_ret(void);  /* 返回 int 可被 main 使用 */

/* ── 断言 ──────────────────────────────────────────────── */
void unity_assert_true(int condition, const char *file, int line, const char *msg);
void unity_assert_false(int condition, const char *file, int line, const char *msg);
void unity_assert_eq_int(int expected, int actual, const char *file, int line);
void unity_assert_eq_uint(unsigned int expected, unsigned int actual, const char *file, int line);
void unity_assert_eq_hex(unsigned int expected, unsigned int actual, const char *file, int line);
void unity_assert_eq_str(const char *expected, const char *actual, const char *file, int line);
void unity_assert_eq_ptr(void *expected, void *actual, const char *file, int line);
void unity_assert_not_null(void *ptr, const char *file, int line);
void unity_assert_in_range(int min, int max, int actual, const char *file, int line);

/* ── 便捷宏 ────────────────────────────────────────────── */
#define TEST_ASSERT_TRUE(cond)     unity_assert_true(cond, __FILE__, __LINE__, #cond)
#define TEST_ASSERT_FALSE(cond)    unity_assert_false(cond, __FILE__, __LINE__, #cond)
#define TEST_ASSERT_EQUAL_INT(a,b) unity_assert_eq_int(a,b,__FILE__,__LINE__)
#define TEST_ASSERT_EQUAL_UINT(a,b) unity_assert_eq_uint(a,b,__FILE__,__LINE__)
#define TEST_ASSERT_EQUAL_HEX(a,b) unity_assert_eq_hex(a,b,__FILE__,__LINE__)
#define TEST_ASSERT_EQUAL_STR(a,b) unity_assert_eq_str(a,b,__FILE__,__LINE__)
#define TEST_ASSERT_EQUAL_PTR(a,b) unity_assert_eq_ptr((void*)(a),(void*)(b),__FILE__,__LINE__)
#define TEST_ASSERT_NOT_NULL(ptr)  unity_assert_not_null((void*)(ptr),__FILE__,__LINE__)
#define TEST_ASSERT_IN_RANGE(a,b,c) unity_assert_in_range(a,b,c,__FILE__,__LINE__)

/* ── 忽略和失败 ────────────────────────────────────────── */
void unity_ignore(const char *file, int line, const char *msg);
#define TEST_IGNORE()           unity_ignore(__FILE__, __LINE__, "ignored")
#define TEST_IGNORE_MSG(msg)    unity_ignore(__FILE__, __LINE__, msg)
#define TEST_FAIL(msg)          unity_assert_true(0, __FILE__, __LINE__, msg)

/* ── 测试组定义 ────────────────────────────────────────── */
#define TEST_GROUP(name)                void name##_run(void)
#define TEST_GROUP_RUNNER(name)         void name##_run(void)
#define TEST(group, name)               static void test_##group##_##name(void)
#define RUN_TEST(group, name)           do { test_##group##_##name(); } while(0)

#ifdef __cplusplus
}
#endif

#endif /* TORK_UNITY_H */
