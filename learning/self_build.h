#ifndef SELF_BUILD_H
#include <time.h>
#define SELF_BUILD_H

#include <stdint.h>

/* ── TORK 自编译引擎 ───────────────────────────────────────
 *  TORK 可以监视自己的源码, 检测到变化后自动编译、测试、替换。
 *  编译失败则回滚, 编译成功则热更新运行中的二进制。
 * ──────────────────────────────────────────────────────── */

#define SB_MAX_SOURCES   64      /* 最多监控64个源文件 */
#define SB_MAX_LOG      2048     /* 编译日志缓冲区 */

/* ── 自编译状态 ──────────────────────────────────────────── */
typedef enum {
    SB_IDLE = 0,        /* 未激活 */
    SB_MONITORING,      /* 监视中 */
    SB_BUILDING,        /* 编译中 */
    SB_TESTING,         /* 测试中 */
    SB_SUCCESS,         /* 编译成功 */
    SB_FAILED           /* 编译失败 */
} sb_state_t;

/* ── 监控的文件 ──────────────────────────────────────────── */
typedef struct {
    char    path[256];          /* 文件路径 */
    time_t  mtime;              /* 上次修改时间 */
    int     changed;            /* 是否已变更 */
} sb_source_t;

/* ── 自编译上下文 ────────────────────────────────────────── */
typedef struct {
    sb_state_t  state;          /* 当前状态 */
    sb_source_t sources[SB_MAX_SOURCES]; /* 监控的源文件 */
    uint32_t    source_count;   /* 源文件数 */
    char        build_log[SB_MAX_LOG];  /* 编译日志 */
    uint32_t    build_count;    /* 累计编译次数 */
    uint32_t    fail_count;     /* 失败次数 */
    uint32_t    success_count;  /* 成功次数 */
    int         interval_ticks; /* 检查间隔(ticks) */
    int         auto_build;     /* 是否自动编译 */
} self_build_t;

/* ── API ─────────────────────────────────────────────────── */

/* 初始化自编译系统: 扫描项目源文件 */
void sb_init(void);

/* 每N个tick调用: 检查源文件是否有变更 */
int sb_check_sources(void);

/* 执行编译: 返回0成功, -1失败 */
int sb_build(void);

/* 热更新: 用新编译的二进制替换运行中的引擎 */
int sb_hotswap(void);

/* 获取状态摘要 */
void sb_summary(char *buf, int buf_size);

/* 保存/加载状态 */
int sb_save(void);
int sb_load(void);

/* 获取内部状态指针 */
self_build_t* sb_get_state(void);

/* 设置自动编译开关 */
void sb_set_auto(int enabled);

#endif /* SELF_BUILD_H */
