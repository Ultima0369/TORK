#include "rollback.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>

/* ── 内部状态 ────────────────────────────────────────────── */

static rb_system_t rb_sys;

/* ── CRC32 ──────────────────────────────────────────────── */

static uint32_t crc32_table[256];
static int crc32_ready = 0;

static void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (c >> 1) ^ 0xEDB88320U : c >> 1;
        crc32_table[i] = c;
    }
    crc32_ready = 1;
}

static uint32_t rb_crc32(const void *buf, size_t len) {
    if (!crc32_ready) crc32_init();
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++)
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

/* ── 时间戳 ────────────────────────────────────────────── */

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ── 路径辅助 ────────────────────────────────────────────── */

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 0;
    return mkdir(path, 0755);
}

/* ── 模块查找 ────────────────────────────────────────────── */

static rb_module_t *find_module(const char *name) {
    for (int i = 0; i < rb_sys.module_count; i++) {
        if (strcmp(rb_sys.modules[i].name, name) == 0)
            return &rb_sys.modules[i];
    }
    return NULL;
}

static rb_module_t *find_or_create_module(const char *name) {
    rb_module_t *mod = find_module(name);
    if (mod) return mod;
    if (rb_sys.module_count >= RB_MAX_MODULES) return NULL;
    mod = &rb_sys.modules[rb_sys.module_count++];
    memset(mod, 0, sizeof(*mod));
    strncpy(mod->name, name, RB_MODULE_NAME_MAX - 1);
    return mod;
}

/* ── 备份文件路径 ────────────────────────────────────────── */

static void backup_path(const char *module_name, int version,
                         char *out, size_t out_size) {
    snprintf(out, out_size, "%s/%s.v%d.bak",
             RB_BACKUP_DIR, module_name, version);
}

/* ── API 实现 ────────────────────────────────────────────── */

int rb_init(void) {
    memset(&rb_sys, 0, sizeof(rb_sys));
    if (ensure_dir(RB_BACKUP_DIR) != 0) return RB_ERR_IO;
    rb_sys.initialized = 1;

    /* 扫描已有备份，恢复统计信息 */
    DIR *dir = opendir(RB_BACKUP_DIR);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            /* 解析文件名: module.vN.bak */
            char *dot = strrchr(entry->d_name, '.');
            if (!dot || strcmp(dot, ".bak") != 0) continue;
            char *v = strstr(entry->d_name, ".v");
            if (!v) continue;
            /* 提取模块名 */
            int name_len = (int)(v - entry->d_name);
            if (name_len <= 0 || name_len >= RB_MODULE_NAME_MAX) continue;
            char mod_name[RB_MODULE_NAME_MAX];
            strncpy(mod_name, entry->d_name, name_len);
            mod_name[name_len] = '\0';
            rb_module_t *mod = find_or_create_module(mod_name);
            if (!mod) continue;
            mod->record_count++;
            rb_sys.total_backups++;
        }
        closedir(dir);
    }

    return RB_OK;
}

int rb_snapshot(const char *module_name, const char *file_path,
                const void *data, size_t len) {
    if (!rb_sys.initialized) return RB_ERR_INIT;
    if (!module_name || !file_path || !data || len == 0) return RB_ERR_INVARG;

    rb_module_t *mod = find_or_create_module(module_name);
    if (!mod) return RB_ERR_NOMEM;

    /* 先清理旧备份 */
    rb_prune(module_name);

    /* 为新版本腾出索引位置: 所有记录后移 */
    int new_idx = mod->record_count;
    if (new_idx >= RB_MAX_VERSIONS) {
        /* 移除最旧版本 */
        for (int i = 1; i < RB_MAX_VERSIONS; i++)
            mod->records[i - 1] = mod->records[i];
        new_idx = RB_MAX_VERSIONS - 1;
        mod->record_count = RB_MAX_VERSIONS;
    }

    /* 记录备份信息 */
    rb_record_t *rec = &mod->records[new_idx];
    memset(rec, 0, sizeof(*rec));
    rec->timestamp_ns = now_ns();
    rec->crc32 = rb_crc32(data, len);
    rec->data_len = (uint16_t)(len > UINT16_MAX ? UINT16_MAX : len);
    strncpy(rec->file_path, file_path, RB_PATH_MAX - 1);

    /* 写入备份文件 */
    char path[RB_PATH_MAX];
    backup_path(module_name, new_idx, path, sizeof(path));

    FILE *f = fopen(path, "wb");
    if (!f) return RB_ERR_IO;
    if (fwrite(data, 1, len, f) != len) {
        fclose(f);
        unlink(path);
        return RB_ERR_IO;
    }
    fclose(f);

    mod->record_count++;
    rb_sys.total_backups++;

    /* 执行编译测试 */
    int compile_ok = rb_compile_test(file_path, data, len);
    rec->compile_pass = (compile_ok == 1) ? 1 : 0;

    return RB_OK;
}

int rb_restore(const char *module_name, int version,
               void *buf, size_t *len) {
    if (!rb_sys.initialized) return RB_ERR_INIT;
    if (!module_name || !buf || !len) return RB_ERR_INVARG;

    rb_module_t *mod = find_module(module_name);
    if (!mod) return RB_ERR_NOENT;

    /* version: 0=最新, 1=次新, ... */
    int idx = mod->record_count - 1 - version;
    if (idx < 0 || idx >= mod->record_count) return RB_ERR_NOENT;

    char path[RB_PATH_MAX];
    backup_path(module_name, idx, path, sizeof(path));

    /* 读取备份文件 */
    FILE *f = fopen(path, "rb");
    if (!f) return RB_ERR_IO;

    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    rewind(f);

    if (file_len <= 0 || (size_t)file_len > *len) {
        fclose(f);
        return RB_ERR_NOMEM;
    }

    size_t read_len = fread(buf, 1, file_len, f);
    fclose(f);
    if ((long)read_len != file_len) return RB_ERR_IO;

    *len = read_len;

    /* CRC 验证 */
    uint32_t expected_crc = mod->records[idx].crc32;
    uint32_t actual_crc = rb_crc32(buf, read_len);
    if (actual_crc != expected_crc) return RB_ERR_CRC;

    rb_sys.total_restores++;
    return RB_OK;
}

int rb_auto_recover(const char *module_name, const char *file_path,
                    void *buf, size_t *len) {
    if (!rb_sys.initialized) return RB_ERR_INIT;
    if (!module_name || !buf || !len) return RB_ERR_INVARG;

    rb_module_t *mod = find_module(module_name);
    if (!mod) return RB_ERR_NOENT;

    if (mod->record_count == 0) return RB_ERR_NOENT;

    /* 从最新版本开始尝试 */
    for (int v = 0; v < mod->record_count; v++) {
        size_t buf_len = *len;
        int ret = rb_restore(module_name, v, buf, &buf_len);
        if (ret != RB_OK) continue;

        /* 先检查合理性 */
        if (rb_sanity_check(buf, buf_len) != RB_OK) continue;

        /* 再检查编译 */
        if (rb_compile_test(file_path, buf, buf_len) == 1) {
            *len = buf_len;
            mod->auto_recover_attempts++;
            return v;  /* 返回恢复到的版本号 */
        }
    }

    rb_sys.total_failures++;
    return RB_ERR_NOENT;
}

int rb_compile_test(const char *file_path, const void *data, size_t len) {
    if (!file_path || !data || len == 0) return RB_ERR_INVARG;

    /* 写临时文件 */
    const char *tmp_path = "/tmp/rb_compile_test_tmp.s";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return RB_ERR_IO;
    if (fwrite(data, 1, len, f) != len) {
        fclose(f);
        unlink(tmp_path);
        return RB_ERR_IO;
    }
    fclose(f);

    /* 调用 as 编译 */
    const char *obj_path = "/tmp/rb_compile_test_tmp.o";
    pid_t pid = fork();
    if (pid < 0) {
        unlink(tmp_path);
        return RB_ERR_IO;
    }
    if (pid == 0) {
        execl("/usr/bin/as", "as", "-o", obj_path, tmp_path, NULL);
        _exit(1);
    }
    int status;
    waitpid(pid, &status, 0);

    /* 清理临时文件 */
    unlink(obj_path);
    unlink(tmp_path);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return 1;  /* 编译通过 */
    return 0;      /* 编译失败 */
}

int rb_sanity_check(const void *data, size_t len) {
    if (!data || len == 0) return RB_ERR_INVARG;

    /* 1. 文件不能太小（至少 10 字节） */
    if (len < 10) return RB_ERR_SANITY;

    /* 2. 文件不能太大（超过 1MB 不太合理） */
    if (len > 1024 * 1024) return RB_ERR_SANITY;

    /* 3. 不能全是零 */
    const uint8_t *bytes = (const uint8_t *)data;
    int non_zero = 0;
    for (size_t i = 0; i < len && i < 1024; i++) {
        if (bytes[i] != 0) { non_zero = 1; break; }
    }
    if (!non_zero) return RB_ERR_SANITY;

    /* 4. 必须有 `.text` 或指令特征（汇编文件检查） */
    const char *text = (const char *)data;
    int has_insn = 0;
    for (size_t i = 0; i < len && i < 2048; i++) {
        if (text[i] == '\t' && i + 1 < len && ((text[i+1] >= 'a' && text[i+1] <= 'z') ||
                                                text[i+1] == '.')) {
            has_insn = 1;
            break;
        }
    }
    if (!has_insn) return RB_ERR_SANITY;

    return RB_OK;
}

int rb_prune(const char *module_name) {
    if (!rb_sys.initialized) return RB_ERR_INIT;
    if (!module_name) return RB_ERR_INVARG;

    rb_module_t *mod = find_module(module_name);
    if (!mod) return RB_OK;  /* 没有记录就不需要清理 */

    while (mod->record_count > RB_MAX_VERSIONS) {
        /* 删除最旧的备份文件 */
        char path[RB_PATH_MAX];
        backup_path(module_name, 0, path, sizeof(path));
        unlink(path);

        /* 前移所有记录 */
        for (int i = 1; i < mod->record_count; i++)
            mod->records[i - 1] = mod->records[i];
        mod->record_count--;
    }

    return RB_OK;
}

int rb_list(const char *module_name, rb_record_t *records, int max_records) {
    if (!rb_sys.initialized) return RB_ERR_INIT;
    if (!module_name || !records) return RB_ERR_INVARG;

    rb_module_t *mod = find_module(module_name);
    if (!mod) return 0;

    int count = mod->record_count < max_records ? mod->record_count : max_records;
    for (int i = 0; i < count; i++)
        records[i] = mod->records[mod->record_count - 1 - i]; /* 最新在前 */

    return count;
}

void rb_stats(int *total_backups, int *total_restores, int *total_failures) {
    if (total_backups)  *total_backups  = rb_sys.total_backups;
    if (total_restores) *total_restores = rb_sys.total_restores;
    if (total_failures) *total_failures = rb_sys.total_failures;
}

int rb_verify_integrity(void) {
    if (!rb_sys.initialized) return RB_ERR_INIT;

    int failures = 0;
    char path[RB_PATH_MAX];

    for (int m = 0; m < rb_sys.module_count; m++) {
        rb_module_t *mod = &rb_sys.modules[m];
        for (int r = 0; r < mod->record_count; r++) {
            backup_path(mod->name, r, path, sizeof(path));

            FILE *f = fopen(path, "rb");
            if (!f) { failures++; continue; }

            fseek(f, 0, SEEK_END);
            long len = ftell(f);
            rewind(f);

            uint8_t *buf = (uint8_t *)malloc(len);
            if (!buf) { fclose(f); failures++; continue; }

            fread(buf, 1, len, f);
            fclose(f);

            uint32_t actual = rb_crc32(buf, len);
            if (actual != mod->records[r].crc32)
                failures++;

            free(buf);
        }
    }

    return failures == 0 ? RB_OK : RB_ERR_CORRUPT;
}
