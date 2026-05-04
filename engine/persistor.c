#include "persistor.h"
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

#define SOUL_SIZE   96
#define BB_SIZE     4096
#define PARAM_DATA  18
#define RULE_MAX_BYTES (RULE_MAX * RULE_STRUCT_SIZE)

/* ── Helpers ─────────────────────────────────────────────────────── */
static int ensure_dir(void) {
    if (mkdir(PERSIST_DIR, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int backup_file(const char *src, const char *dst) {
    return rename(src, dst);
}

static int write_file(const char *path, const void *data, size_t len) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    ssize_t w = write(fd, data, len);
    fsync(fd);
    close(fd);
    return (w == (ssize_t)len) ? 0 : -1;
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

/* ── Save ───────────────────────────────────────────────────────── */
int ps_save_all(const void *soul_buf, size_t soul_len) {
    if (ensure_dir() != 0) return -1;

    int ok = 0;

    /* Backup old files */
    backup_file(PATH_SOUL, PATH_BAK_SOUL);
    backup_file(PATH_BB, PATH_BAK_BB);
    backup_file(PATH_PARAMS, PATH_BAK_PARAMS);
    backup_file(PATH_RULES, PATH_BAK_RULES);

    /* Save soul (from caller-provided buffer) */
    uint32_t soul_tick = 0;
    if (soul_buf && soul_len >= 4) {
        memcpy(&soul_tick, soul_buf, 4);
        if (write_file(PATH_SOUL, soul_buf, soul_len) != 0)
            fprintf(stderr, "ps_save: soul failed\n");
        else
            ok++;
    } else {
        /* No soul data — remove stale file */
        unlink(PATH_SOUL);
    }

    /* Save params (18 bytes from shared memory) */
    uint8_t param_buf[PARAM_DATA];
    memcpy(param_buf, (const void *)PARAM_ADDR, PARAM_DATA);
    if (write_file(PATH_PARAMS, param_buf, PARAM_DATA) != 0)
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
    if (write_file(PATH_RULES, rule_buf, rule_bytes) != 0)
        fprintf(stderr, "ps_save: rules failed\n");
    else
        ok++;

    /* Save blackboard (4KB) */
    uint8_t bb_buf[BB_SIZE];
    memcpy(bb_buf, (const void *)BB_ADDR, BB_SIZE);
    if (write_file(PATH_BB, bb_buf, BB_SIZE) != 0)
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

    return (ok >= 4) ? 0 : -1;
}

/* ── Restore (bb/params/rules only — soul via ps_restore_soul) ─── */
int ps_restore_all(void) {
    if (access(PERSIST_DIR, F_OK) != 0) return 0;

    int restored = 0;

    /* Try blackboard */
    uint8_t bb_buf[BB_SIZE];
    if (read_file(PATH_BB, bb_buf, BB_SIZE) == 0) {
        memcpy((void *)BB_ADDR, bb_buf, BB_SIZE);
        restored++;
    }

    /* Try params */
    uint8_t param_buf[PARAM_DATA];
    if (read_file(PATH_PARAMS, param_buf, PARAM_DATA) == 0) {
        memcpy((void *)PARAM_ADDR, param_buf, PARAM_DATA);
        restored++;
    }

    /* Try rules — read manifest for rule count first */
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
            memcpy((void *)(RULE_ADDR + 0x10), rule_buf, rule_bytes);
            memcpy((void *)(RULE_ADDR + 8), &rule_count, 4);
            uint32_t magic = RULE_MAGIC;
            memcpy((void *)(RULE_ADDR), &magic, 4);
            restored++;
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

/* ── Emergency save (signal-safe subset) ────────────────────────── */
static volatile int ps_emergency_done = 0;

void ps_emergency_save(void) {
    if (ps_emergency_done) return;
    ps_emergency_done = 1;
    ps_save_all(NULL, 0);
    ps_cleanup_baks();
}

/* ── Cleanup .bak files ─────────────────────────────────────────── */
void ps_cleanup_baks(void) {
    unlink(PATH_BAK_SOUL);
    unlink(PATH_BAK_BB);
    unlink(PATH_BAK_PARAMS);
    unlink(PATH_BAK_RULES);
}
