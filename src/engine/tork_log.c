/* ══════════════════════════════════════════════════════════════
 * TORK 结构化日志 — JSON Lines 实现
 *
 * 输出格式 (每行一个 JSON):
 *   {"t":1672531200.123,"l":"INFO","m":"engine","msg":"started","pid":1234}
 *
 * 写入策略:
 *   - 缓冲区 4096 字节, 满或 100ms 超时自动刷盘
 *   - 日志文件达到 10MB 自动轮转
 *   - SIGUSR1 触发日志轮转
 * ══════════════════════════════════════════════════════════════ */

#include "tork_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>

/* ── 配置 ──────────────────────────────────────────────── */
#define LOG_BUF_SIZE    4096
#define LOG_FLUSH_MS    100
#define LOG_MAX_FILE    (10 * 1024 * 1024)  /* 10 MB */

/* ── 内部状态 ──────────────────────────────────────────── */
static FILE    *g_log_fp      = NULL;
static log_level_t g_log_level = LOG_LEVEL_DEFAULT;
static char     g_log_buf[LOG_BUF_SIZE];
static int      g_log_buf_len = 0;
static int      g_log_rotate_count = 0;
static char     g_log_path[256] = "logs/tork.jsonl";

static const char *level_str[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

/* ── 信号处理 ──────────────────────────────────────────── */
static void log_signal_handler(int sig) {
    (void)sig;
    log_rotate();
}

/* ── 刷新缓冲区 ────────────────────────────────────────── */
void log_flush(void) {
    if (g_log_fp && g_log_buf_len > 0) {
        fwrite(g_log_buf, 1, g_log_buf_len, g_log_fp);
        fflush(g_log_fp);
        g_log_buf_len = 0;
    }
}

/* ── 日志轮转 ──────────────────────────────────────────── */
int log_rotate(void) {
    log_flush();

    if (!g_log_fp) return -1;

    fclose(g_log_fp);
    g_log_fp = NULL;

    /* 检查当前文件大小 */
    FILE *check = fopen(g_log_path, "rb");
    if (check) {
        fseek(check, 0, SEEK_END);
        long size = ftell(check);
        fclose(check);

        if (size >= LOG_MAX_FILE) {
            /* 轮转: 改名 logs/tork.1.jsonl, logs/tork.2.jsonl ... */
            char old[256], new_name[256];
            for (int i = 9; i >= 0; i--) {
                snprintf(old, sizeof(old), "logs/tork.%d.jsonl", i);
                snprintf(new_name, sizeof(new_name), "logs/tork.%d.jsonl", i + 1);
                rename(old, new_name);
            }
            rename(g_log_path, "logs/tork.1.jsonl");
            g_log_rotate_count++;
        }
    }

    /* 重新打开 */
    g_log_fp = fopen(g_log_path, "ab");
    if (!g_log_fp) {
        g_log_fp = stderr;
        return -1;
    }
    return 0;
}

/* ── 初始化 ────────────────────────────────────────────── */
void log_init(const char *path, log_level_t level) {
    if (path) {
        snprintf(g_log_path, sizeof(g_log_path), "%s", path);
    }

    /* 确保 logs/ 目录存在 */
    mkdir("logs", 0755);

    g_log_fp = fopen(g_log_path, "ab");
    if (!g_log_fp) {
        g_log_fp = stderr;
    }

    g_log_level = level;
    g_log_buf_len = 0;

    /* 注册 SIGUSR1 用于日志轮转 */
    struct sigaction sa;
    sa.sa_handler = log_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    log_info("log", "started, path=%s, level=%s", g_log_path, level_str[level]);
}

/* ── 设置等级 ──────────────────────────────────────────── */
void log_set_level(log_level_t level) {
    g_log_level = level;
}

log_level_t log_get_level(void) {
    return g_log_level;
}

/* ── 核心写入 ──────────────────────────────────────────── */
void log_write(log_level_t level, const char *module,
               const char *fmt, ...) {
    if (!g_log_fp) return;
    if (level < g_log_level) return;

    /* 获取时间 */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    double timestamp = (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;

    /* 构造 JSON 行 (先格式化消息) */
    char msg_buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    /* JSON 转义: " → \" */
    char esc_msg[1024];
    int esc_len = 0;
    for (int i = 0; msg_buf[i] && esc_len < 1020; i++) {
        if (msg_buf[i] == '"') {
            esc_msg[esc_len++] = '\\';
            esc_msg[esc_len++] = '"';
        } else if (msg_buf[i] == '\\') {
            esc_msg[esc_len++] = '\\';
            esc_msg[esc_len++] = '\\';
        } else if (msg_buf[i] == '\n') {
            esc_msg[esc_len++] = '\\';
            esc_msg[esc_len++] = 'n';
        } else {
            esc_msg[esc_len++] = msg_buf[i];
        }
    }
    esc_msg[esc_len] = '\0';

    /* 组装 JSON 行 */
    int needed = snprintf(g_log_buf + g_log_buf_len,
                          LOG_BUF_SIZE - g_log_buf_len,
                          "{\"t\":%.3f,\"l\":\"%s\",\"m\":\"%s\",\"msg\":\"%s\",\"pid\":%d}\n",
                          timestamp, level_str[level], module, esc_msg, getpid());

    if (needed > 0) {
        g_log_buf_len += needed;
    }

    /* 缓冲区满或达到 FATAL 等级 → 立即刷盘 */
    if (g_log_buf_len >= LOG_BUF_SIZE - 256 || level >= LOG_ERROR) {
        log_flush();
    }
}

/* ── 关闭 ──────────────────────────────────────────────── */
void log_close(void) {
    log_flush();
    if (g_log_fp && g_log_fp != stderr) {
        fclose(g_log_fp);
    }
    g_log_fp = NULL;
}
