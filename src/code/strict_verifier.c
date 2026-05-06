#include "strict_verifier.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

/* ── 内部状态 ────────────────────────────────────────────── */

static sv_frozen_t g_frozen[SV_MAX_FROZEN];
static int g_frozen_count = 0;
static int g_initialized = 0;

/* ── 初始化 ────────────────────────────────────────────── */

void sv_init(void) {
    memset(g_frozen, 0, sizeof(g_frozen));
    g_frozen_count = 0;
    g_initialized = 1;
    sv_load();
}

/* ── 严格编译验证 (文件路径) ────────────────────────────── */

sv_result_t sv_verify(const char *src_path, const char *work_dir) {
    sv_result_t result;
    memset(&result, 0, sizeof(result));

    if (!src_path) {
        result.compile_ok = 0;
        result.error_count = 1;
        snprintf(result.first_error, sizeof(result.first_error), "%s", "null source path");
        return result;
    }

    /* 构造编译命令: gcc -Wall -Wextra -Werror -O2 -c */
    char cmd[1024];
    char obj_path[512];
    snprintf(obj_path, sizeof(obj_path), "%s/sv_verify.o",
             work_dir ? work_dir : "/tmp/tork_sv");
    snprintf(cmd, sizeof(cmd),
             "mkdir -p %s && "
             "gcc -Wall -Wextra -Werror -O2 "
             "-Isrc/engine -Isrc/instinct -Isrc/code -Isrc/core "
             "-Isrc/install -Isrc/sandbox -Isrc/learning -Isrc/persist "
             "-c -o %s %s 2>&1",
             work_dir ? work_dir : "/tmp/tork_sv",
             obj_path, src_path);

    /* 执行编译，捕获输出 */
    FILE *p = popen(cmd, "r");
    if (!p) {
        result.compile_ok = 0;
        result.error_count = 1;
        snprintf(result.first_error, sizeof(result.first_error), "%s", "popen failed");
        return result;
    }

    char line[512];
    while (fgets(line, sizeof(line), p)) {
        /* 解析错误和警告 */
        if (strstr(line, "error:")) {
            result.error_count++;
            if (result.first_error[0] == '\0')
                snprintf(result.first_error, sizeof(result.first_error),
                         "%.200s", line);
        }
        if (strstr(line, "warning:")) {
            result.warning_count++;
            if (result.first_warning[0] == '\0')
                snprintf(result.first_warning, sizeof(result.first_warning),
                         "%.200s", line);
        }
    }

    int rc = pclose(p);
    result.compile_ok = (rc == 0 && result.error_count == 0 && result.warning_count == 0) ? 1 : 0;

    /* 清理 .o 文件 */
    unlink(obj_path);

    return result;
}

/* ── 严格编译验证 (内存缓冲) ────────────────────────────── */

sv_result_t sv_verify_buf(const char *src_buf, int src_len,
                           const char *suffix, const char *work_dir) {
    sv_result_t result;
    memset(&result, 0, sizeof(result));

    if (!src_buf || src_len <= 0) {
        result.compile_ok = 0;
        result.error_count = 1;
        snprintf(result.first_error, sizeof(result.first_error), "%s", "null/empty source");
        return result;
    }

    const char *dir = (work_dir && work_dir[0]) ? work_dir : "/tmp/tork_sv";
    char src_path[512];
    snprintf(src_path, sizeof(src_path), "%s/sv_verify_%s", dir,
             suffix ? suffix : "tmp.c");

    /* 写源码到临时文件 */
    mkdir(dir, 0755);
    FILE *f = fopen(src_path, "w");
    if (!f) {
        result.compile_ok = 0;
        result.error_count = 1;
        snprintf(result.first_error, sizeof(result.first_error), "%s", "cannot write temp file");
        return result;
    }
    fwrite(src_buf, 1, src_len, f);
    fclose(f);

    result = sv_verify(src_path, dir);

    /* 清理临时源文件 */
    unlink(src_path);

    return result;
}

/* ── 冷冻状态管理 ────────────────────────────────────────────── */

int sv_record_result(uint16_t strategy_id, int compile_ok,
                      uint32_t current_round) {
    /* 查找已有的冷冻记录 */
    int idx = -1;
    for (int i = 0; i < g_frozen_count; i++) {
        if (g_frozen[i].strategy_id == strategy_id) {
            idx = i;
            break;
        }
    }

    if (idx < 0 && g_frozen_count < SV_MAX_FROZEN) {
        idx = g_frozen_count;
        g_frozen[idx].strategy_id = strategy_id;
        g_frozen[idx].fail_streak = 0;
        g_frozen[idx].frozen_until = 0;
        g_frozen_count++;
    }

    if (idx < 0) return 0;  /* 冷冻表满，不记录 */

    sv_frozen_t *entry = &g_frozen[idx];

    if (compile_ok) {
        entry->fail_streak = 0;
        entry->frozen_until = 0;
        return 0;
    }

    entry->fail_streak++;

    if (entry->fail_streak >= SV_FREEZE_THRESHOLD) {
        entry->frozen_until = current_round + SV_FREEZE_ROUNDS;
        return 1;  /* 策略被冷冻 */
    }

    return 0;
}

int sv_is_frozen(uint16_t strategy_id, uint32_t current_round) {
    for (int i = 0; i < g_frozen_count; i++) {
        if (g_frozen[i].strategy_id == strategy_id) {
            if (g_frozen[i].frozen_until > 0 &&
                current_round < g_frozen[i].frozen_until)
                return 1;
            /* 冷冻期已过，解冻 */
            if (g_frozen[i].frozen_until > 0 &&
                current_round >= g_frozen[i].frozen_until) {
                g_frozen[i].frozen_until = 0;
                g_frozen[i].fail_streak = 0;
            }
            return 0;
        }
    }
    return 0;
}

int sv_fail_streak(uint16_t strategy_id) {
    for (int i = 0; i < g_frozen_count; i++) {
        if (g_frozen[i].strategy_id == strategy_id)
            return g_frozen[i].fail_streak;
    }
    return 0;
}

float sv_decay_fitness(float raw_fitness, uint16_t strategy_id,
                        uint32_t current_round) {
    if (sv_is_frozen(strategy_id, current_round))
        return raw_fitness * 0.1f;

    int streak = sv_fail_streak(strategy_id);
    if (streak > 0)
        return raw_fitness * 0.5f;

    return raw_fitness;
}

int sv_frozen_list(sv_frozen_t *out, int max_count) {
    int count = 0;
    for (int i = 0; i < g_frozen_count && count < max_count; i++) {
        if (g_frozen[i].frozen_until > 0) {
            out[count] = g_frozen[i];
            count++;
        }
    }
    return count;
}

/* ── 持久化 ────────────────────────────────────────────── */

#define SV_PATH "persist/strict_verifier.bin"
#define SV_MAGIC 0x53560000  /* "SV\0\0" */

int sv_save(void) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", SV_PATH);

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    uint32_t magic = SV_MAGIC;
    if (write(fd, &magic, 4) != 4) { close(fd); unlink(tmp); return -1; }
    if (write(fd, &g_frozen_count, 4) != 4) { close(fd); unlink(tmp); return -1; }
    size_t data_len = g_frozen_count * sizeof(sv_frozen_t);
    if (data_len > 0 && write(fd, g_frozen, data_len) != (ssize_t)data_len) {
        close(fd); unlink(tmp); return -1;
    }
    fsync(fd);
    close(fd);

    if (rename(tmp, SV_PATH) != 0) { unlink(tmp); return -1; }
    return 0;
}

int sv_load(void) {
    int fd = open(SV_PATH, O_RDONLY);
    if (fd < 0) return -1;

    uint32_t magic;
    if (read(fd, &magic, 4) != 4 || magic != SV_MAGIC) {
        close(fd); return -1;
    }

    int count;
    if (read(fd, &count, 4) != 4 || count > SV_MAX_FROZEN) {
        close(fd); return -1;
    }

    size_t data_len = count * sizeof(sv_frozen_t);
    if (data_len > 0 && read(fd, g_frozen, data_len) != (ssize_t)data_len) {
        close(fd); return -1;
    }

    g_frozen_count = count;
    close(fd);
    return 0;
}

void sv_cleanup(void) {
    sv_save();
}