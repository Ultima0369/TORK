/* ══════════════════════════════════════════════════════════════
 * TORK 结构化日志 — JSON Lines
 *
 * 所有模块统一通过此模块输出日志。
 * 每行一个 JSON 对象，包含时间戳、等级、模块、消息。
 * 可被 logstash/filebeat/fluentd 直接摄取。
 *
 * 等级: DEBUG < INFO < WARN < ERROR < FATAL
 * 运行时可通过 SIGNAL 动态调整等级。
 * ══════════════════════════════════════════════════════════════ */

#ifndef TORK_LOG_H
#define TORK_LOG_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 日志等级 ──────────────────────────────────────────── */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3,
    LOG_FATAL = 4,
} log_level_t;

#define LOG_LEVEL_DEFAULT  LOG_INFO

/* ── 初始化 ────────────────────────────────────────────── */
void  log_init(const char *path, log_level_t level);
void  log_set_level(log_level_t level);
log_level_t log_get_level(void);

/* ── 核心日志 API ──────────────────────────────────────── */
void log_write(log_level_t level, const char *module,
               const char *fmt, ...) __attribute__((format(printf,3,4)));

/* ── 便捷宏 ────────────────────────────────────────────── */
#define log_debug(mod, fmt, ...)  log_write(LOG_DEBUG, mod, fmt, ##__VA_ARGS__)
#define log_info(mod, fmt, ...)   log_write(LOG_INFO,  mod, fmt, ##__VA_ARGS__)
#define log_warn(mod, fmt, ...)   log_write(LOG_WARN,  mod, fmt, ##__VA_ARGS__)
#define log_error(mod, fmt, ...)  log_write(LOG_ERROR, mod, fmt, ##__VA_ARGS__)
#define log_fatal(mod, fmt, ...)  log_write(LOG_FATAL, mod, fmt, ##__VA_ARGS__)

/* ── 工具 ──────────────────────────────────────────────── */
void log_flush(void);       /* 强制刷盘 */
void log_close(void);       /* 关闭日志文件 */
int  log_rotate(void);      /* 日志轮转: 0=成功, <0=失败 */

#ifdef __cplusplus
}
#endif

#endif /* TORK_LOG_H */
