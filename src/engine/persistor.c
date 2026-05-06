#include "persistor.h"
#include "soul_access.h"
#include "blackboard.h"
#include "calibrator.h"
#include "inductor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

/* ── Paths ──────────────────────────────────────────────────────── */
#define PATH_SOUL       PERSIST_DIR "soul.bin"
#define PATH_BB         PERSIST_DIR "blackboard.bin"
#define PATH_PARAMS     PERSIST_DIR "params.bin"
#define PATH_RULES      PERSIST_DIR "rules.bin"
#define PATH_COMMIT     PERSIST_DIR ".commit_tick"
#define PATH_MANIFEST   PERSIST_DIR "manifest.json"
#define PATH_BAK_SOUL   PERSIST_DIR "soul.bak"
#define PATH_BAK_BB     PERSIST_DIR "blackboard.bak"
#define PATH_BAK_PARAMS PERSIST_DIR "params.bak"
#define PATH_BAK_RULES  PERSIST_DIR "rules.bak"
#define PATH_PARAMS_LOG PERSIST_DIR "params_history.log"

/* ── Shared memory addresses ────────────────────────────────────── */
#define BB_ADDR     0x300000
#define PARAM_ADDR  0x301000
#define RULE_ADDR   0x302000

/* SOUL_SIZE from soul_access.h (192) */
#define BB_SIZE     4096
#define PARAM_DATA  18
#define PARAM_OFF_DATA 0x008  /* from calibrator.h */
#define RULE_MAX_BYTES (RULE_MAX * RULE_STRUCT_SIZE)

/* ── Cal init guard ───────────────────────────────────────────── */
static volatile int g_cal_initialized = 0;

void ps_mark_cal_initialized(void) {
    g_cal_initialized = 1;
}

/* ── Helpers ─────────────────────────────────────────────────────── */
static int ensure_dir(void) {
    if (mkdir(PERSIST_DIR, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

/* Write to a temp file, then atomically rename to target.
   On failure, the target is untouched.
   Includes readback verification after rename. */
static int safe_write_file(const char *path, const void *data, size_t len,
                           const char *bak_path) {
    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    ssize_t w = write(fd, data, len);
    fsync(fd);
    close(fd);

    if (w != (ssize_t)len) {
        unlink(tmp_path);
        return -1;
    }

    /* Move old file to backup (may not exist, that's ok) */
    if (bak_path) rename(path, bak_path);

    /* Atomically install new file */
    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        return -1;
    }

    /* Readback verification */
    uint8_t *verify = (uint8_t *)malloc(len);
    if (!verify) return 0;  /* verification skipped if OOM, write succeeded */
    int vfd = open(path, O_RDONLY);
    if (vfd < 0) { free(verify); return -1; }
    ssize_t r = read(vfd, verify, len);
    close(vfd);
    int ok = (r == (ssize_t)len && memcmp(verify, data, len) == 0);
    free(verify);
    return ok ? 0 : -1;
}

static int read_file(const char *path, void *buf, size_t len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t r = read(fd, buf, len);
    close(fd);
    return (r == (ssize_t)len) ? 0 : -1;
}

static uint32_t file_checksum(const char *path) {
    uint8_t buf[8192];
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    uint32_t crc = 0xFFFFFFFF;
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            crc ^= buf[i];
            for (int b = 0; b < 8; b++)
                crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }
    close(fd);
    return ~crc;
}

/* ── 从 manifest 读取指定字段的 CRC 字符串 ────────────────────── */
static int read_crc_from_manifest(const char *field, char *out, size_t out_len) {
    FILE *mf = fopen(PATH_MANIFEST, "r");
    if (!mf) return -1;
    int found = 0;
    char line[256];
    while (fgets(line, sizeof(line), mf)) {
        char fname[64];
        char crc_val[64];
        if (sscanf(line, "  \"%64[^\"]\": \"0x%64[^\"]\"", fname, crc_val) == 2) {
            if (strcmp(fname, field) == 0) {
                snprintf(out, out_len, "%s", crc_val);
                found = 1;
                break;
            }
        }
    }
    fclose(mf);
    return found ? 0 : -1;
}

/* ── 加载前 CRC 校验：失败时尝试 .bak 回退 ──────────────────── */
static int verify_or_restore_bak(const char *path, const char *bak_path,
                                  const char *crc_field) {
    char expected_crc[64] = "";
    if (read_crc_from_manifest(crc_field, expected_crc, sizeof(expected_crc)) != 0)
        return -1;  /* 无 manifest → 无法校验 */
    
    uint32_t expected = (uint32_t)strtoul(expected_crc, NULL, 16);
    if (expected == 0) return -1;
    uint32_t actual = file_checksum(path);
    
    if (actual == expected)
        return 0;  /* CRC 匹配，文件完好 */
    
    /* CRC 不匹配：尝试从 .bak 恢复 */
    fprintf(stderr, "ps_verify: %s CRC mismatch (expected 0x%08X, got 0x%08X), trying .bak\n",
            path, expected, actual);
    
    if (!bak_path || access(bak_path, F_OK) != 0)
        return -1;  /* 无备份可用 */
    
    if (rename(bak_path, path) != 0)
        return -1;
    
    /* 验证备份文件的 CRC */
    actual = file_checksum(path);
    if (actual == expected) {
        fprintf(stderr, "ps_verify: restored %s from .bak\n", path);
        return 0;
    }
    
    fprintf(stderr, "ps_verify: .bak also corrupted for %s\n", path);
    return -1;
}

/* ── Save ───────────────────────────────────────────────────────── */
/* ── 事务提交标记：写入 .commit_tick 表示本次保存已完成 ──────── */
static int write_commit_marker(uint32_t tick) {
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%u\n", tick);
    if (n <= 0) return -1;
    int fd = open(PATH_COMMIT, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    ssize_t w = write(fd, buf, (size_t)n);
    fsync(fd);
    close(fd);
    return (w == n) ? 0 : -1;
}

int ps_save_all(const void *soul_buf, size_t soul_len) {
    if (ensure_dir() != 0) return -1;

    /* 清除旧提交标记：如果后续崩溃，标记不存在 → 加载回退备份 */
    unlink(PATH_COMMIT);

    int any_failed = 0;
    int ok = 0;

    /* Save soul (from caller-provided buffer) */
    uint32_t soul_tick = 0;
    if (soul_buf && soul_len >= 4) {
        memcpy(&soul_tick, soul_buf, 4);
        if (safe_write_file(PATH_SOUL, soul_buf, soul_len, PATH_BAK_SOUL) != 0) {
            fprintf(stderr, "ps_save: soul failed\n");
            any_failed = 1;
        } else {
            ok++;
        }
    } else {
        unlink(PATH_SOUL);
    }

    /* Save params (18 bytes from shared memory) */
    uint8_t param_buf[PARAM_DATA];
    memcpy(param_buf, (const void *)(PARAM_ADDR + PARAM_OFF_DATA), PARAM_DATA);
    if (safe_write_file(PATH_PARAMS, param_buf, PARAM_DATA, PATH_BAK_PARAMS) != 0)
        fprintf(stderr, "ps_save: params failed\n");
    else
        ok++;

    /* Save rules (from shared memory, up to count*96) */
    uint8_t rule_buf[RULE_MAX_BYTES];
    memset(rule_buf, 0, sizeof(rule_buf));
    uint32_t rule_count = 0;
    memcpy(&rule_count, (const void *)(RULE_ADDR + 8), 4);
    size_t rule_bytes = (size_t)rule_count * RULE_STRUCT_SIZE;
    if (rule_bytes > sizeof(rule_buf)) rule_bytes = sizeof(rule_buf);
    if (rule_count > 0)
        memcpy(rule_buf, (const void *)(RULE_ADDR + 0x10), rule_bytes);
    if (safe_write_file(PATH_RULES, rule_buf, rule_bytes, PATH_BAK_RULES) != 0)
        fprintf(stderr, "ps_save: rules failed\n");
    else
        ok++;

    /* Save blackboard (4KB) */
    uint8_t bb_buf[BB_SIZE];
    memcpy(bb_buf, (const void *)BB_ADDR, BB_SIZE);
    if (safe_write_file(PATH_BB, bb_buf, BB_SIZE, PATH_BAK_BB) != 0)
        fprintf(stderr, "ps_save: blackboard failed\n");
    else
        ok++;

    /* Write manifest */
    FILE *mf = fopen(PATH_MANIFEST, "w");
    if (mf) {
        time_t now = time(NULL);
        fprintf(mf, "{\n");
        fprintf(mf, "  \"version\": 1,\n");
        fprintf(mf, "  \"tick\": %u,\n", soul_tick);
        fprintf(mf, "  \"timestamp\": %ld,\n", (long)now);
        fprintf(mf, "  \"soul_crc\": \"0x%08X\",\n", file_checksum(PATH_SOUL));
        fprintf(mf, "  \"bb_crc\": \"0x%08X\",\n", file_checksum(PATH_BB));
        fprintf(mf, "  \"params_crc\": \"0x%08X\",\n", file_checksum(PATH_PARAMS));
        fprintf(mf, "  \"rules_crc\": \"0x%08X\",\n", file_checksum(PATH_RULES));
        fprintf(mf, "  \"rule_count\": %u\n", rule_count);
        fprintf(mf, "}\n");
        fclose(mf);
        ok++;
    }

    /* 写入提交标记：manifest 写完后才写。如果之前有任何文件写失败，不提交 */
    if (!any_failed && write_commit_marker(soul_tick) == 0)
        ok++;
    else if (any_failed)
        fprintf(stderr, "ps_save: 核心文件写失败，跳过提交标记\n");

    return (ok >= 4) ? 0 : -1;
}

/* ── Restore (bb/params/rules only — soul via ps_restore_soul) ─── */
int ps_restore_all(void) {
    if (access(PERSIST_DIR, F_OK) != 0) return 0;

    /* 事务完整性检查：.commit_tick 不存在 → 上次保存未完成，回退备份 */
    if (access(PATH_COMMIT, F_OK) != 0) {
        fprintf(stderr, "ps_restore: 未检测到提交标记，尝试 .bak 恢复\n");
        /* 尝试从 .bak 恢复所有文件 */
        if (access(PATH_BAK_SOUL, F_OK) == 0) rename(PATH_BAK_SOUL, PATH_SOUL);
        if (access(PATH_BAK_BB, F_OK) == 0) rename(PATH_BAK_BB, PATH_BB);
        if (access(PATH_BAK_PARAMS, F_OK) == 0) rename(PATH_BAK_PARAMS, PATH_PARAMS);
        if (access(PATH_BAK_RULES, F_OK) == 0) rename(PATH_BAK_RULES, PATH_RULES);
    }

    int restored = 0;

    /* Try blackboard — 带 CRC 校验 */
    uint8_t bb_buf[BB_SIZE];
    if (read_file(PATH_BB, bb_buf, BB_SIZE) == 0) {
        if (verify_or_restore_bak(PATH_BB, PATH_BAK_BB, "bb_crc") == 0) {
            /* 校验通过（或已从 .bak 恢复），重新读取 */
            if (read_file(PATH_BB, bb_buf, BB_SIZE) == 0) {
                memcpy((void *)BB_ADDR, bb_buf, BB_SIZE);
                restored++;
            }
        } else {
            /* 校验失败且无 .bak：直接加载（尽力而为） */
            memcpy((void *)BB_ADDR, bb_buf, BB_SIZE);
            restored++;
            fprintf(stderr, "ps_restore: bb loaded without CRC verification\n");
        }
    }

    /* Try params — 带 CRC 校验 */
    uint8_t param_buf[PARAM_DATA];
    if (read_file(PATH_PARAMS, param_buf, PARAM_DATA) == 0) {
        if (verify_or_restore_bak(PATH_PARAMS, PATH_BAK_PARAMS, "params_crc") == 0) {
            if (read_file(PATH_PARAMS, param_buf, PARAM_DATA) == 0) {
                memcpy((void *)(PARAM_ADDR + PARAM_OFF_DATA), param_buf, PARAM_DATA);
                restored++;
            }
        } else {
            memcpy((void *)(PARAM_ADDR + PARAM_OFF_DATA), param_buf, PARAM_DATA);
            restored++;
            fprintf(stderr, "ps_restore: params loaded without CRC verification\n");
        }
    }

    /* Try rules — 从 manifest 读 rule_count + CRC 校验 */
    uint32_t rule_count = 0;
    FILE *mf = fopen(PATH_MANIFEST, "r");
    if (mf) {
        char line[256];
        while (fgets(line, sizeof(line), mf)) {
            if (sscanf(line, " \"rule_count\": %u", &rule_count) == 1)
                break;
        }
        fclose(mf);
    }

    if (rule_count > 0 && rule_count <= RULE_MAX) {
        size_t rule_bytes = (size_t)rule_count * RULE_STRUCT_SIZE;
        uint8_t *rule_buf = (uint8_t *)malloc(rule_bytes);
        if (rule_buf && read_file(PATH_RULES, rule_buf, rule_bytes) == 0) {
            if (verify_or_restore_bak(PATH_RULES, PATH_BAK_RULES, "rules_crc") == 0) {
                if (read_file(PATH_RULES, rule_buf, rule_bytes) == 0) {
                    memcpy((void *)(RULE_ADDR + 0x10), rule_buf, rule_bytes);
                    memcpy((void *)(RULE_ADDR + 8), &rule_count, 4);
                    uint32_t magic = RULE_MAGIC;
                    memcpy((void *)(RULE_ADDR), &magic, 4);
                    restored++;
                }
            } else {
                memcpy((void *)(RULE_ADDR + 0x10), rule_buf, rule_bytes);
                memcpy((void *)(RULE_ADDR + 8), &rule_count, 4);
                uint32_t magic = RULE_MAGIC;
                memcpy((void *)(RULE_ADDR), &magic, 4);
                restored++;
                fprintf(stderr, "ps_restore: rules loaded without CRC verification\n");
            }
        }
        free(rule_buf);
    }

    return restored;
}

/* ── Restore soul into caller buffer ────────────────────────────── */
size_t ps_restore_soul(void *buf, size_t len) {
    if (len < SOUL_SIZE) return 0;
    if (read_file(PATH_SOUL, buf, SOUL_SIZE) != 0) return 0;
    return SOUL_SIZE;
}

/* ── Decay ─────────────────────────────────────────────────────── */
int ps_decay_memory(void) {
    struct tork_rule rules[RULE_MAX];
    int n = ind_load_rules(rules, RULE_MAX);

    for (int i = 0; i < n; i++) {
        if (rules[i].confidence <= RULE_CONFIDENCE_RETIRE &&
            rules[i].apply_count < 5) {
            if (rules[i].confidence != 1) {
                rules[i].confidence = 1;
                ind_update_rule(i, &rules[i]);
                printf("IND: marking rule slot %d for decay (conf<30%%, apply<5)\n", i);
            }
        } else if (rules[i].confidence == 1) {
            if (rules[i].apply_count == 0) {
                memset(&rules[i], 0, sizeof(rules[i]));
                ind_update_rule(i, &rules[i]);
                printf("IND: forgot rule slot %d (no recovery)\n", i);
            } else {
                rules[i].confidence = 50;
                ind_update_rule(i, &rules[i]);
                printf("IND: rule slot %d recovered from decay\n", i);
            }
        }
    }

    /* Log params (default values, old calibrator removed) */
    FILE *log = fopen(PATH_PARAMS_LOG, "a");
    if (log) {
        fprintf(log, "%ld tw=35 tm=50 tc=65 fw=10 dw=20 cw=30 cc=10 ac=30 nc=50\n",
                (long)time(NULL));
        fclose(log);
    }

    return 0;
}

/* ── Hot-swap ──────────────────────────────────────────────────── */
int ps_hot_swap(const char *new_binary_path) {
    if (ps_save_all(NULL, 0) != 0) {
        fprintf(stderr, "hot_swap: save failed, aborting\n");
        return -1;
    }

    pid_t child = fork();
    if (child < 0) return -1;

    if (child == 0) {
        execl(new_binary_path, new_binary_path, "--restore", NULL);
        _exit(1);
    }

    for (int i = 0; i < 60; i++) {
        usleep(500000);
        int st;
        pid_t r = waitpid(child, &st, WNOHANG);
        if (r == child) {
            fprintf(stderr, "hot_swap: child died, continuing\n");
            return -1;
        }
        if (access(PATH_MANIFEST, F_OK) == 0) {
            _exit(0);
        }
    }

    fprintf(stderr, "hot_swap: timeout, continuing\n");
    kill(child, SIGTERM);
    waitpid(child, NULL, 0);
    return -1;
}

/* ── Emergency save (signal-safe: only async-safe syscalls) ──── */
static volatile int ps_emergency_done = 0;
static const void *g_soul_buf = NULL;
static size_t g_soul_buf_len = 0;

void ps_register_soul_buf(const void *buf, size_t len) {
    g_soul_buf = buf;
    g_soul_buf_len = len;
}

void ps_emergency_save(void) {
    if (ps_emergency_done) return;
    ps_emergency_done = 1;
    int fd = open(PATH_SOUL, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        if (g_soul_buf && g_soul_buf_len >= SOUL_SIZE) {
            write(fd, g_soul_buf, SOUL_SIZE);
        }
        close(fd);
    }
}

/* ── Cleanup .bak files ─────────────────────────────────────────── */
void ps_cleanup_baks(void) {
    unlink(PATH_BAK_SOUL);
    unlink(PATH_BAK_BB);
    unlink(PATH_BAK_PARAMS);
    unlink(PATH_BAK_RULES);
}
