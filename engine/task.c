#include "task.h"
#include "dispatch.h"
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

    return 0;
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

/* ── 执行一个待处理任务 ────────────────────────────────────
 * 通过 dispatch 闭环执行 — 结果自动写入 experience
 */
void task_process_one(void) {
    if (!g_initialized) return;

    for (int i = 0; i < TASK_MAX_SLOTS; i++) {
        task_entry_t *t = &g_queue.slots[i];
        if (!t->active || t->status != TASK_PENDING) continue;

        t->status = TASK_RUNNING;
        char local_input[TASK_MAX_INPUT];
        snprintf(local_input, sizeof(local_input), "%s", t->input);

        dispatch_action_t action;
        switch (t->type) {
        case TASK_EXEC:       action = DISP_EXEC_CMD;      break;
        case TASK_ANALYZE:    action = DISP_ANALYZE_ASM;   break;
        case TASK_AUDIT:      action = DISP_AUDIT_CODE;    break;
        default:              action = DISP_NUM_ACTIONS;    break;
        }

        if (action >= DISP_NUM_ACTIONS) {
            snprintf(t->output, TASK_MAX_OUTPUT, "{\"error\":\"unknown task type\"}");
            t->status = TASK_FAILED;
            g_queue.total_failed++;
            return;
        }

        dispatch_input_t din;
        memset(&din, 0, sizeof(din));
        din.action      = action;
        din.input       = local_input;
        din.timeout_sec = 30;

        dispatch_output_t dout = tork_dispatch(&din);
        t->exit_code = dout.exit_code;

        int copy_len = dout.output_len;
        if (copy_len >= TASK_MAX_OUTPUT) copy_len = TASK_MAX_OUTPUT - 1;
        memcpy(t->output, dout.output, copy_len);
        t->output[copy_len] = '\0';

        if (dout.rc == 0) {
            t->status = TASK_DONE;
            g_queue.total_completed++;
        } else {
            t->status = TASK_FAILED;
            g_queue.total_failed++;
        }

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