#ifndef TASK_H
#define TASK_H

#include <stdint.h>

/*
 * TORK 任务队列：吃饭的手艺
 *
 * TORK 不只是自言自语——它能接收外部任务、执行、返回结果。
 * 这是 TORK 挣口饭吃的管道。
 */

#define TASK_MAX_SLOTS    16
#define TASK_MAX_INPUT    512
#define TASK_MAX_OUTPUT   8192
#define TASK_MAX_CMD      512

/* 任务类型 */
typedef enum {
    TASK_EXEC,       /* 执行命令 (通过 sandbox) */
    TASK_ANALYZE,    /* 分析汇编文件 */
    TASK_AUDIT,      /* 代码安全审计 */
    TASK_UNKNOWN_TYPE
} task_type_t;

/* 任务状态 */
typedef enum {
    TASK_PENDING,    /* 等待执行 */
    TASK_RUNNING,    /* 正在执行 */
    TASK_DONE,       /* 执行完成 */
    TASK_FAILED,     /* 执行失败 */
    TASK_CANCELLED,  /* 已取消 */
    TASK_NOT_FOUND   /* 任务不存在 */
} task_status_t;

/* 任务条目 */
typedef struct {
    uint32_t     id;              /* 任务 ID (递增) */
    task_type_t  type;            /* 任务类型 */
    task_status_t status;         /* 当前状态 */
    char         input[TASK_MAX_INPUT];   /* 输入（命令/文件路径） */
    char         output[TASK_MAX_OUTPUT]; /* 输出结果 */
    int          exit_code;       /* 退出码 */
    uint32_t     submit_tick;     /* 提交时刻 */
    uint32_t     finish_tick;     /* 完成时刻 */
    uint8_t      active;          /* 1=有效槽位 */
} task_entry_t;

/* 任务队列 */
typedef struct {
    task_entry_t slots[TASK_MAX_SLOTS];
    uint32_t     next_id;         /* 下一个任务 ID */
    uint32_t     total_submitted;
    uint32_t     total_completed;
    uint32_t     total_failed;
} task_queue_t;

/* ── Public API ───────────────────────────────────────────── */

/* 初始化任务队列 */
void task_init(void);

/* 提交任务，返回任务 ID (0=失败) */
uint32_t task_submit(task_type_t type, const char *input);

/* 获取任务状态 */
task_status_t task_status(uint32_t id);

/* 获取任务结果 (复制到 out)，返回 0=成功 -1=任务不存在或未完成 */
int task_result(uint32_t id, task_entry_t *out);

/* 在主循环中执行一个待处理任务 (非阻塞，每次最多执行一个) */
void task_process_one(void);

/* 取消任务 */
int task_cancel(uint32_t id);

/* 获取统计 */
int task_pending_count(void);
int task_active_count(void);
uint32_t task_total_completed(void);
uint32_t task_total_failed(void);

/* 清理 */
void task_cleanup(void);

#endif
