#include "task.h"
#include "sandbox.h"
#include "code_reader.h"
#include "auditor.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── 全局队列 ─────────────────────────────────────────────── */
static task_queue_t g_queue;
static int g_initialized = 0;

/* ── 初始化 ──────────────────────────────────────────────── */
void task_init(void) {
    memset(&g_queue, 0, sizeof(g_queue));
    g_queue.next_id = 1;
    g_initialized = 1;
    printf("  TASK: task queue initialized (%d slots)\n", TASK_MAX_SLOTS);
}

/* ── 提交任务 ────────────────────────────────────────────── */
uint32_t task_submit(task_type_t type, const char *input) {
    if (!g_initialized || !input) return 0;

    /* 找空闲槽位 */
    for (int i = 0; i < TASK_MAX_SLOTS; i++) {
        if (!g_queue.slots[i].active) {
            task_entry_t *t = &g_queue.slots[i];
            memset(t, 0, sizeof(task_entry_t));
            t->id = g_queue.next_id++;
            t->type = type;
            t->status = TASK_PENDING;
            snprintf(t->input, TASK_MAX_INPUT, "%s", input);
            t->active = 1;
            g_queue.total_submitted++;
            return t->id;
        }
    }

    /* 队列满，尝试覆盖已完成的 */
    for (int i = 0; i < TASK_MAX_SLOTS; i++) {
        if (g_queue.slots[i].active &&
            (g_queue.slots[i].status == TASK_DONE ||
             g_queue.slots[i].status == TASK_FAILED)) {
            task_entry_t *t = &g_queue.slots[i];
            memset(t, 0, sizeof(task_entry_t));
            t->id = g_queue.next_id++;
            t->type = type;
            t->status = TASK_PENDING;
            snprintf(t->input, TASK_MAX_INPUT, "%s", input);
            t->active = 1;
            g_queue.total_submitted++;
            return t->id;
        }
    }

    return 0;  /* 真的满了 */
}

/* ── 获取任务状态 ────────────────────────────────────────── */
task_status_t task_status(uint32_t id) {
    if (!g_initialized) return TASK_NOT_FOUND;
    for (int i = 0; i < TASK_MAX_SLOTS; i++) {
        if (g_queue.slots[i].active && g_queue.slots[i].id == id)
            return g_queue.slots[i].status;
    }
    return TASK_NOT_FOUND;
}

/* ── 获取任务结果 ────────────────────────────────────────── */
int task_result(uint32_t id, task_entry_t *out) {
    if (!g_initialized || !out) return -1;
    for (int i = 0; i < TASK_MAX_SLOTS; i++) {
        if (g_queue.slots[i].active && g_queue.slots[i].id == id) {
            if (g_queue.slots[i].status != TASK_DONE &&
                g_queue.slots[i].status != TASK_FAILED)
                return -1;
            *out = g_queue.slots[i];
            return 0;
        }
    }
    return -1;
}

/* ── 执行一个待处理任务 ──────────────────────────────────── */
void task_tick(void) {
    if (!g_initialized) return;

    for (int i = 0; i < TASK_MAX_SLOTS; i++) {
        task_entry_t *t = &g_queue.slots[i];
        if (!t->active || t->status != TASK_PENDING) continue;

        /* 找到一个待处理任务，执行它 */
        t->status = TASK_RUNNING;
        char local_input[TASK_MAX_INPUT];
        snprintf(local_input, sizeof(local_input), "%s", t->input);

        switch (t->type) {
        case TASK_EXEC: {
            sandbox_result_t sr = sandbox_exec(local_input, 30);

            t->exit_code = sr.exit_code;
            /* JSON 转义后嵌入 */
            char stdout_esc[SANDBOX_MAX_STDOUT * 2];
            char stderr_esc[SANDBOX_MAX_STDERR * 2];
            {
                int j = 0;
                for (int i = 0; sr.stdout_buf[i] && j < (int)sizeof(stdout_esc) - 2; i++) {
                    switch (sr.stdout_buf[i]) {
                    case '"':  stdout_esc[j++] = '\\'; stdout_esc[j++] = '"';  break;
                    case '\\': stdout_esc[j++] = '\\'; stdout_esc[j++] = '\\'; break;
                    case '\n': stdout_esc[j++] = '\\'; stdout_esc[j++] = 'n';  break;
                    case '\r': stdout_esc[j++] = '\\'; stdout_esc[j++] = 'r';  break;
                    case '\t': stdout_esc[j++] = '\\'; stdout_esc[j++] = 't';  break;
                    default:   stdout_esc[j++] = sr.stdout_buf[i]; break;
                    }
                }
                stdout_esc[j] = '\0';
                j = 0;
                for (int i = 0; sr.stderr_buf[i] && j < (int)sizeof(stderr_esc) - 2; i++) {
                    switch (sr.stderr_buf[i]) {
                    case '"':  stderr_esc[j++] = '\\'; stderr_esc[j++] = '"';  break;
                    case '\\': stderr_esc[j++] = '\\'; stderr_esc[j++] = '\\'; break;
                    case '\n': stderr_esc[j++] = '\\'; stderr_esc[j++] = 'n';  break;
                    case '\r': stderr_esc[j++] = '\\'; stderr_esc[j++] = 'r';  break;
                    case '\t': stderr_esc[j++] = '\\'; stderr_esc[j++] = 't';  break;
                    default:   stderr_esc[j++] = sr.stderr_buf[i]; break;
                    }
                }
                stderr_esc[j] = '\0';
            }
            snprintf(t->output, TASK_MAX_OUTPUT,
                "{\"exit_code\":%d,\"stdout\":\"%s\",\"stderr\":\"%s\",\"timed_out\":%s}",
                sr.exit_code, stdout_esc, stderr_esc,
                sr.timed_out ? "true" : "false");

            if (sr.exit_code == 0) {
                t->status = TASK_DONE;
                g_queue.total_completed++;
            } else {
                t->status = TASK_FAILED;
                g_queue.total_failed++;
            }
            break;
        }

        case TASK_ANALYZE: {
            /* 用 code_reader 分析汇编文件 */
            char asm_buf[8192];
            int alen = asm_read_file(local_input, asm_buf, sizeof(asm_buf));

            if (alen <= 0) {
                snprintf(t->output, TASK_MAX_OUTPUT,
                    "{\"error\":\"cannot read %s\"}", local_input);
                t->status = TASK_FAILED;
                g_queue.total_failed++;
                break;
            }

            int cm = 0, ca = 0, cc = 0, co = 0;
            asm_classify_insns(asm_buf, alen, "memcpy_tork", &cm, &ca, &cc, &co);
            int insns = asm_count_insns_in_func(asm_buf, alen, "memcpy_tork");

            char opcodes[32][8];
            asm_extract_opcodes(asm_buf, alen, "memcpy_tork", opcodes, 32);

            /* 构建 JSON 输出 */
            int off = 0;
            off += snprintf(t->output + off, TASK_MAX_OUTPUT - off,
                "{\"file\":\"%s\",\"insns\":%d,", local_input, insns);
            off += snprintf(t->output + off, TASK_MAX_OUTPUT - off,
                "\"classify\":{\"mov\":%d,\"arith\":%d,\"ctrl\":%d,\"other\":%d},",
                cm, ca, cc, co);
            off += snprintf(t->output + off, TASK_MAX_OUTPUT - off,
                "\"opcodes\":[");
            int max_opcodes = (insns > 10) ? 10 : insns;
            for (int j = 0; j < max_opcodes && j < 32; j++) {
                if (j > 0) off += snprintf(t->output + off, TASK_MAX_OUTPUT - off, ",");
                off += snprintf(t->output + off, TASK_MAX_OUTPUT - off, "\"%s\"", opcodes[j]);
            }
            off += snprintf(t->output + off, TASK_MAX_OUTPUT - off, "]}");

            t->exit_code = 0;
            t->status = TASK_DONE;
            g_queue.total_completed++;
            break;
        }

        case TASK_AUDIT: {
            /* 代码安全审计 — TORK 的手艺 */
            audit_result_t ar = audit_asm_file(local_input, NULL);
            int json_len = audit_result_to_json(&ar, t->output, TASK_MAX_OUTPUT);
            if (json_len < 0) {
                snprintf(t->output, TASK_MAX_OUTPUT,
                    "{\"error\":\"audit serialization failed\"}");
                t->status = TASK_FAILED;
                g_queue.total_failed++;
            } else {
                t->exit_code = 0;
                t->status = TASK_DONE;
                g_queue.total_completed++;
            }
            break;
        }

        default:
            snprintf(t->output, TASK_MAX_OUTPUT, "{\"error\":\"unknown task type\"}");
            t->status = TASK_FAILED;
            g_queue.total_failed++;
            break;
        }

        /* 每次只执行一个任务，避免阻塞主循环 */
        return;
    }
}

/* ── 取消任务 ────────────────────────────────────────────── */
int task_cancel(uint32_t id) {
    if (!g_initialized) return -1;
    for (int i = 0; i < TASK_MAX_SLOTS; i++) {
        if (g_queue.slots[i].active && g_queue.slots[i].id == id) {
            if (g_queue.slots[i].status == TASK_PENDING ||
                g_queue.slots[i].status == TASK_RUNNING) {
                g_queue.slots[i].status = TASK_CANCELLED;
                return 0;
            }
            return -1;
        }
    }
    return -1;
}

/* ── 统计 ────────────────────────────────────────────────── */
int task_pending_count(void) {
    if (!g_initialized) return 0;
    int count = 0;
    for (int i = 0; i < TASK_MAX_SLOTS; i++)
        if (g_queue.slots[i].active && g_queue.slots[i].status == TASK_PENDING)
            count++;
    return count;
}

int task_active_count(void) {
    if (!g_initialized) return 0;
    int count = 0;
    for (int i = 0; i < TASK_MAX_SLOTS; i++)
        if (g_queue.slots[i].active) count++;
    return count;
}

uint32_t task_total_completed(void) {
    if (!g_initialized) return 0;
    return g_queue.total_completed;
}

uint32_t task_total_failed(void) {
    if (!g_initialized) return 0;
    return g_queue.total_failed;
}

/* ── 清理 ────────────────────────────────────────────────── */
void task_cleanup(void) {
    memset(&g_queue, 0, sizeof(g_queue));
    g_initialized = 0;
    printf("  TASK: task queue cleaned up\n");
}