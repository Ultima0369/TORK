/* ══════════════════════════════════════════════════════════════
 * Unity — C 单元测试框架 (TORK 嵌入版) 实现
 * ══════════════════════════════════════════════════════════════ */

#include "unity.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

unity_state_t Unity;

void unity_init(void) {
    memset(&Unity, 0, sizeof(Unity));
}

void unity_begin(const char *name) {
    printf("\n━━━ %s ━━━\n\n", name);
}

void unity_end(void) {
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  %d tests  |  %d passed  |  %d failed  |  %d ignored\n\n",
           Unity.total_tests, Unity.passed, Unity.failed, Unity.ignored);
}

void unity_end_with_ret(void) {
    unity_end();
    exit(Unity.failed > 0 ? 1 : 0);
}

static void unity_fail(const char *file, int line, const char *msg) {
    Unity.failed++;
    printf("  ❌ FAIL: %s:%d: %s\n", file, line, msg);
}

static void unity_pass(void) {
    Unity.passed++;
}

/* ── 断言实现 ──────────────────────────────────────────── */

void unity_assert_true(int condition, const char *file, int line, const char *msg) {
    Unity.total_tests++;
    if (condition) {
        unity_pass();
    } else {
        unity_fail(file, line, msg);
    }
}

void unity_assert_false(int condition, const char *file, int line, const char *msg) {
    unity_assert_true(!condition, file, line, msg);
}

void unity_assert_eq_int(int expected, int actual, const char *file, int line) {
    Unity.total_tests++;
    if (expected == actual) {
        unity_pass();
    } else {
        char buf[128];
        snprintf(buf, sizeof(buf), "Expected %d, got %d", expected, actual);
        unity_fail(file, line, buf);
    }
}

void unity_assert_eq_uint(unsigned int expected, unsigned int actual, const char *file, int line) {
    Unity.total_tests++;
    if (expected == actual) {
        unity_pass();
    } else {
        char buf[128];
        snprintf(buf, sizeof(buf), "Expected %u, got %u", expected, actual);
        unity_fail(file, line, buf);
    }
}

void unity_assert_eq_hex(unsigned int expected, unsigned int actual, const char *file, int line) {
    Unity.total_tests++;
    if (expected == actual) {
        unity_pass();
    } else {
        char buf[128];
        snprintf(buf, sizeof(buf), "Expected 0x%x, got 0x%x", expected, actual);
        unity_fail(file, line, buf);
    }
}

void unity_assert_eq_str(const char *expected, const char *actual, const char *file, int line) {
    Unity.total_tests++;
    if (!expected && !actual) { unity_pass(); return; }
    if (expected && actual && strcmp(expected, actual) == 0) {
        unity_pass();
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf), "Expected \"%s\", got \"%s\"",
                 expected ? expected : "NULL",
                 actual ? actual : "NULL");
        unity_fail(file, line, buf);
    }
}

void unity_assert_eq_ptr(void *expected, void *actual, const char *file, int line) {
    Unity.total_tests++;
    if (expected == actual) {
        unity_pass();
    } else {
        char buf[128];
        snprintf(buf, sizeof(buf), "Expected %p, got %p", expected, actual);
        unity_fail(file, line, buf);
    }
}

void unity_assert_not_null(void *ptr, const char *file, int line) {
    Unity.total_tests++;
    if (ptr != NULL) {
        unity_pass();
    } else {
        unity_fail(file, line, "Expected non-NULL pointer");
    }
}

void unity_assert_in_range(int min, int max, int actual, const char *file, int line) {
    Unity.total_tests++;
    if (actual >= min && actual <= max) {
        unity_pass();
    } else {
        char buf[128];
        snprintf(buf, sizeof(buf), "Expected %d-%d, got %d", min, max, actual);
        unity_fail(file, line, buf);
    }
}

void unity_ignore(const char *file, int line, const char *msg) {
    Unity.total_tests++;
    Unity.ignored++;
    printf("  ⚠️  IGNORE: %s:%d: %s\n", file, line, msg);
}
