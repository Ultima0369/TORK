#ifndef WATCHER_H
#define WATCHER_H

#include <stdint.h>

/* ── TORK 环境观察器 ─────────────────────────────────────────
 *  静默观察用户的工作环境, 学习工具链使用模式。
 *  不干预, 不修改, 只看。
 * ──────────────────────────────────────────────────────────── */

#define WATCH_MAX_EVENTS    256     /* 环形事件缓冲区 */
#define WATCH_MAX_PATTERNS  64      /* 可识别的模式数 */
#define WATCH_SAMPLE_INTERVAL 10    /* 每10tick采样一次 */

/* ── 观察事件类型 ──────────────────────────────────────────── */
typedef enum {
    WATCH_EVENT_NONE = 0,
    WATCH_EVENT_FILE_OPEN,      /* 文件被打开 */
    WATCH_EVENT_FILE_SAVE,      /* 文件被保存 */
    WATCH_EVENT_COMPILE,        /* 编译发生 */
    WATCH_EVENT_GIT_COMMIT,     /* git commit */
    WATCH_EVENT_PROCESS_SPAWN,  /* 新进程启动 */
    WATCH_EVENT_TERMINAL_CMD,   /* 终端命令 */
    WATCH_EVENT_ERROR,          /* 错误输出 */
    WATCH_EVENT_USER_IDLE,      /* 用户闲置 */
    WATCH_EVENT_USER_ACTIVE,    /* 用户恢复活动 */
    WATCH_EVENT_NET_CONNECT,    /* 网络连接 */
} watch_event_type_t;

/* ── 单个观察事件 ──────────────────────────────────────────── */
typedef struct {
    uint64_t        tick;           /* 事件发生时的心跳 */
    watch_event_type_t type;        /* 事件类型 */
    uint32_t        pid;            /* 相关进程PID */
    uint32_t        duration_ticks; /* 持续时间 */
    int8_t          importance;     /* 0-10 重要程度 */
    uint8_t         category;       /* 自定义分类 */
    char            brief[48];      /* 简短描述 */
} watch_event_t;

/* ── 观察到的模式 ──────────────────────────────────────────── */
typedef struct {
    uint32_t        id;                 /* 模式ID */
    watch_event_type_t trigger_event;   /* 触发事件 */
    watch_event_type_t follow_event;    /* 后续事件 */
    uint32_t        typical_gap;        /* 典型间隔(tick) */
    uint32_t        count;              /* 出现次数 */
    float           confidence;         /* 置信度 0-1 */
    char            description[64];    /* 模式描述 */
} watch_pattern_t;

/* ── 观察器状态 ────────────────────────────────────────────── */
typedef struct {
    watch_event_t   ring[WATCH_MAX_EVENTS];    /* 环形事件缓冲区 */
    uint32_t        head;                       /* 最新事件索引 */
    uint32_t        count;                      /* 总事件数 */
    
    watch_pattern_t patterns[WATCH_MAX_PATTERNS]; /* 已识别的模式 */
    uint32_t        pattern_count;
    
    uint64_t        last_active_tick;           /* 上次用户活动tick */
    uint32_t        idle_threshold;             /* 闲置判定阈值 */
    uint8_t         is_active;                  /* 用户是否活跃 */
    
    /* 统计 */
    uint32_t        total_compilations;
    uint32_t        total_errors;
    uint32_t        total_saves;
} watcher_t;

/* ── Public API ────────────────────────────────────────────── */

/* 初始化观察器 */
void watcher_init(void);

/* 记录一个观察事件 */
void watcher_record(watch_event_type_t type, uint32_t pid, 
                    const char *brief, int8_t importance);

/* 扫描 /proc 检测用户活动 (每10tick调用) */
void watcher_scan_proc(void);

/* 分析事件缓冲区, 识别重复模式 */
int watcher_learn_patterns(void);

/* 获取当前观察摘要 */
void watcher_summary(char *buf, int buf_size);

/* 保存观察数据到磁盘 */
int watcher_save(void);

/* 从磁盘加载观察数据 */
int watcher_load(void);

/* 获取观察器内部状态指针 (供instinct/决策使用) */
watcher_t* watcher_get_state(void);

#endif /* WATCHER_H */
