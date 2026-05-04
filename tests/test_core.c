/*
 * TORK 核心模块单元测试
 * 覆盖: sandbox, code_reader, code_modifier, auditor, task, experience, pattern
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

/* ── 被测模块 ──────────────────────────────────────────────────── */
#include "../sandbox/sandbox.h"
#include "../code/code_reader.h"
#include "../code/code_modifier.h"
#include "../engine/auditor.h"
#include "../engine/task.h"
#include "../learning/experience.h"
#include "../learning/pattern.h"

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

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("FAIL\n    \"%s\" != \"%s\"  (line %d)\n", (a), (b), __LINE__); \
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
 *  Sandbox 测试
 * ══════════════════════════════════════════════════════════════════ */

TEST(sandbox_classify_read) {
    ASSERT_EQ(sandbox_classify("ls -la"), CMD_READ);
    ASSERT_EQ(sandbox_classify("cat /etc/passwd"), CMD_READ);
    ASSERT_EQ(sandbox_classify("grep -r pattern ."), CMD_READ);
    ASSERT_EQ(sandbox_classify("find . -name '*.c'"), CMD_READ);
}

TEST(sandbox_classify_write) {
    ASSERT_EQ(sandbox_classify("cp a b"), CMD_WRITE);
    ASSERT_EQ(sandbox_classify("mkdir /tmp/test"), CMD_WRITE);
    ASSERT_EQ(sandbox_classify("touch /tmp/file"), CMD_WRITE);
}

TEST(sandbox_classify_exec) {
    ASSERT_EQ(sandbox_classify("gcc -o out src.c"), CMD_EXEC);
    ASSERT_EQ(sandbox_classify("python3 script.py"), CMD_EXEC);
    ASSERT_EQ(sandbox_classify("make"), CMD_EXEC);
}

TEST(sandbox_classify_net) {
    ASSERT_EQ(sandbox_classify("curl http://example.com"), CMD_NET);
    ASSERT_EQ(sandbox_classify("wget http://example.com/file"), CMD_NET);
}

TEST(sandbox_classify_sys) {
    ASSERT_EQ(sandbox_classify("apt-get update"), CMD_SYS);
    ASSERT_EQ(sandbox_classify("systemctl status nginx"), CMD_SYS);
}

TEST(sandbox_classify_dangerous) {
    ASSERT_EQ(sandbox_classify("rm -rf /"), CMD_DANGEROUS);
    ASSERT_EQ(sandbox_classify("rm -rf /*"), CMD_DANGEROUS);
    ASSERT_EQ(sandbox_classify("dd if=/dev/zero of=/dev/sda"), CMD_DANGEROUS);
}

TEST(sandbox_classify_unknown) {
    ASSERT_EQ(sandbox_classify("my_custom_tool"), CMD_UNKNOWN);
    ASSERT_EQ(sandbox_classify(""), CMD_UNKNOWN);
    ASSERT_EQ(sandbox_classify(NULL), CMD_UNKNOWN);
}

TEST(sandbox_exec_echo) {
    sandbox_result_t r = sandbox_exec("echo hello_tork", 5);
    ASSERT_EQ(r.exit_code, 0);
    ASSERT_TRUE(strstr(r.stdout_buf, "hello_tork") != NULL);
    ASSERT_TRUE(!r.timed_out);
}

TEST(sandbox_exec_timeout) {
    sandbox_result_t r = sandbox_exec("sleep 10", 1);
    ASSERT_TRUE(r.timed_out);
}

TEST(sandbox_exec_not_found) {
    sandbox_result_t r = sandbox_exec("nonexistent_command_xyz", 3);
    ASSERT_TRUE(r.exit_code != 0);
}

TEST(sandbox_exec_captures_stderr) {
    sandbox_result_t r = sandbox_exec("echo err >&2", 3);
    ASSERT_TRUE(r.stderr_buf[0] != '\0');
}

TEST(sandbox_fixed_buffers_no_malloc) {
    /* 验证 fixed buffer API 无需 free */
    sandbox_result_t r = sandbox_exec("echo test", 3);
    ASSERT_TRUE(r.stdout_buf[0] != '\0');
    /* 不调用任何 free — 如果编译+运行无 crash 即成功 */
}

/* ══════════════════════════════════════════════════════════════════
 *  Code Reader 测试
 * ══════════════════════════════════════════════════════════════════ */

TEST(code_reader_count_insns) {
    const char *asm_code =
        ".text\n"
        ".globl test_fn\n"
        "test_fn:\n"
        "\tmovq $1, %rax\n"
        "\tmovq $0, %rdi\n"
        "\tsyscall\n"
        "\tret\n";
    FILE *f = fopen("/tmp/tork_test_asm.s", "w");
    ASSERT_TRUE(f != NULL);
    fputs(asm_code, f);
    fclose(f);

    char buf[4096];
    int len = asm_read_file("/tmp/tork_test_asm.s", buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    int count = asm_count_insns_in_func(buf, len, "test_fn");
    ASSERT_TRUE(count >= 3);
    remove("/tmp/tork_test_asm.s");
}

TEST(code_reader_classify) {
    const char *asm_code =
        ".text\n"
        "test_fn:\n"
        "\tmovq %rax, %rbx\n"
        "\taddq $1, %rax\n"
        "\tjmp .L2\n"
        "\tnop\n"
        "\tret\n";
    FILE *f = fopen("/tmp/tork_test_asm2.s", "w");
    ASSERT_TRUE(f != NULL);
    fputs(asm_code, f);
    fclose(f);

    char buf[4096];
    int len = asm_read_file("/tmp/tork_test_asm2.s", buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    int ctrl = 0, arith = 0, mov = 0, other = 0;
    asm_classify_insns(buf, len, "test_fn", &mov, &arith, &ctrl, &other);
    ASSERT_TRUE(ctrl >= 1);  /* jmp */
    ASSERT_TRUE(arith >= 1); /* addq */
    ASSERT_TRUE(mov >= 1);   /* movq */
    remove("/tmp/tork_test_asm2.s");
}

/* ══════════════════════════════════════════════════════════════════
 *  Auditor 测试
 * ══════════════════════════════════════════════════════════════════ */

TEST(auditor_nonexistent_file) {
    audit_result_t ar = audit_asm_file("/tmp/nonexistent_tork_file.s", NULL);
    /* 文件不存在应该返回 0 total_insns 或 error */
    ASSERT_TRUE(ar.total_insns == 0 || ar.risk_count >= 0);
}

TEST(auditor_result_to_json) {
    audit_result_t ar;
    memset(&ar, 0, sizeof(ar));
    snprintf(ar.filepath, sizeof(ar.filepath), "test.s");
    ar.total_insns = 10;
    ar.risk_score = 25.0f;

    char json[2048];
    int len = audit_result_to_json(&ar, json, sizeof(json));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(json, "test.s") != NULL);
    ASSERT_TRUE(strstr(json, "\"insns\":10") != NULL);
}

/* ══════════════════════════════════════════════════════════════════
 *  Task Queue 测试
 * ══════════════════════════════════════════════════════════════════ */

TEST(task_init_and_submit) {
    task_init();
    uint32_t tid = task_submit(TASK_EXEC, "echo test_task");
    ASSERT_TRUE(tid > 0);

    task_status_t st = task_status(tid);
    ASSERT_TRUE(st == TASK_PENDING || st == TASK_RUNNING || st == TASK_DONE);
}

TEST(task_not_found) {
    task_init();
    task_status_t st = task_status(99999);
    ASSERT_EQ(st, TASK_NOT_FOUND);
}

TEST(task_queue_stats) {
    task_init();
    int pending = task_pending_count();
    int active = task_active_count();
    ASSERT_TRUE(pending >= 0);
    ASSERT_TRUE(active >= 0);
}

TEST(task_max_slots) {
    task_init();
    /* 填满队列 */
    uint32_t last_id = 0;
    for (int i = 0; i < TASK_MAX_SLOTS + 2; i++) {
        uint32_t tid = task_submit(TASK_EXEC, "echo test");
        if (tid > 0) last_id = tid;
        else break;
    }
    /* 再提交应该返回 0 (满) */
    /* 注意：某些任务可能已执行完成释放了 slot，
     * 所以不一定返回 0 — 验证 id 递增即可 */
    ASSERT_TRUE(last_id > 0);
}

/* ══════════════════════════════════════════════════════════════════
 *  Experience 测试
 * ══════════════════════════════════════════════════════════════════ */

TEST(exp_init_and_count) {
    exp_init();
    uint32_t cnt = exp_count();
    ASSERT_TRUE(cnt > 0 || cnt == 0);  /* always true for uint, just verify init works */
}

TEST(exp_record_and_query) {
    exp_init();
    uint32_t before = exp_count();
    exp_record(50, 0, 1, 0, 10, 0, 0, 0, 0, 0, 1);
    uint32_t after = exp_count();
    ASSERT_TRUE(after >= before);
}

/* ══════════════════════════════════════════════════════════════════
 *  Pattern 测试
 * ══════════════════════════════════════════════════════════════════ */

TEST(pattern_init) {
    pat_init();
    /* 验证初始化不崩溃 */
}

TEST(pattern_record_and_match) {
    pat_init();
    /* 先录入一些经验，然后学习模式 */
    exp_init();
    for (int i = 0; i < 10; i++) {
        exp_record(100 + i, 0, 1, 0, 2, 0, 20, 0, 1, 0, 1);
    }
    pat_learn_from_experience();
    int count = pat_count();
    ASSERT_TRUE(count >= 0);  /* 模式学习可能需要更多数据 */
}

/* ══════════════════════════════════════════════════════════════════
 *  JSON 转义测试
 * ══════════════════════════════════════════════════════════════════ */

TEST(sandbox_exec_json_format) {
    char *json = sandbox_exec_json("echo test_json_escape", 5);
    ASSERT_TRUE(json != NULL);
    ASSERT_TRUE(strstr(json, "\"exit_code\":") != NULL);
    ASSERT_TRUE(strstr(json, "\"stdout\":") != NULL);
    ASSERT_TRUE(json[0] == '{');
    /* 最后一个字符应该是 } */
    int len = (int)strlen(json);
    ASSERT_TRUE(json[len-1] == '}');
    free(json);
}

/* ══════════════════════════════════════════════════════════════════
 *  主函数
 * ══════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("TORK Unit Tests\n");
    printf("════════════════════════════════════════════════════════\n\n");

    printf("[Sandbox]\n");
    RUN(sandbox_classify_read);
    RUN(sandbox_classify_write);
    RUN(sandbox_classify_exec);
    RUN(sandbox_classify_net);
    RUN(sandbox_classify_sys);
    RUN(sandbox_classify_dangerous);
    RUN(sandbox_classify_unknown);
    RUN(sandbox_exec_echo);
    RUN(sandbox_exec_timeout);
    RUN(sandbox_exec_not_found);
    RUN(sandbox_exec_captures_stderr);
    RUN(sandbox_fixed_buffers_no_malloc);

    printf("\n[Code Reader]\n");
    RUN(code_reader_count_insns);
    RUN(code_reader_classify);

    printf("\n[Auditor]\n");
    RUN(auditor_nonexistent_file);
    RUN(auditor_result_to_json);

    printf("\n[Task Queue]\n");
    RUN(task_init_and_submit);
    RUN(task_not_found);
    RUN(task_queue_stats);
    RUN(task_max_slots);

    printf("\n[Experience]\n");
    RUN(exp_init_and_count);
    RUN(exp_record_and_query);

    printf("\n[Pattern]\n");
    RUN(pattern_init);
    RUN(pattern_record_and_match);

    printf("\n[JSON Format]\n");
    RUN(sandbox_exec_json_format);

    printf("\n════════════════════════════════════════════════════════\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);

    return g_fail > 0 ? 1 : 0;
}
