#include "code_archive.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

/* ── 内部状态 ────────────────────────────────────────────── */

static ca_header_t ca_hdr;
static ca_record_t ca_records[CA_MAX_RECORDS];
static int ca_dirty = 0;
static int ca_last_alive_idx = -1;  /* 当前存活的记录索引 */

/* ── 时间戳 ────────────────────────────────────────────── */

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ── CRC32 ────────────────────────────────────────────── */

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

uint32_t ca_crc32(const void *buf, size_t len) {
    if (!crc32_ready) crc32_init();
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++)
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

/* ── 初始化 ────────────────────────────────────────────── */

void ca_init(void) {
    memset(&ca_hdr, 0, sizeof(ca_hdr));
    memset(ca_records, 0, sizeof(ca_records));
    ca_hdr.magic = CA_MAGIC;
    ca_hdr.version = CA_VERSION;
    ca_hdr.capacity = CA_MAX_RECORDS;
    ca_hdr.created_ns = now_ns();
    ca_hdr.modified_ns = ca_hdr.created_ns;
    ca_dirty = 0;
    ca_last_alive_idx = -1;

    if (ca_load() != 0) {
        /* 首次运行，空档案 */
        ca_hdr.count = 0;
    }
}

/* ── 记录一次变异 ────────────────────────────────────── */

int ca_record(uint32_t code_hash, uint8_t compile_ok,
              uint16_t strategy_id, const char *description) {
    if (ca_hdr.count >= CA_MAX_RECORDS) {
        /* 环形缓冲：覆盖最旧的 */
        memmove(&ca_records[0], &ca_records[1],
                (CA_MAX_RECORDS - 1) * sizeof(ca_record_t));
        ca_hdr.count = CA_MAX_RECORDS - 1;
    }

    int idx = ca_hdr.count;
    ca_record_t *r = &ca_records[idx];
    memset(r, 0, sizeof(*r));

    r->timestamp = now_ns();
    r->code_hash = code_hash;
    r->compile_ok = compile_ok;
    r->strategy_id = strategy_id;
    r->survival_ticks = 0;
    r->last_tick = 0;
    r->death_cause = compile_ok ? CA_DEATH_ALIVE : CA_DEATH_COMPILE_FAIL;
    r->fitness_at_death = 0;
    if (description)
        strncpy(r->description, description, sizeof(r->description) - 1);

    ca_hdr.count++;
    ca_hdr.modified_ns = now_ns();
    ca_dirty = 1;

    /* 如果编译通过，标记为当前存活 */
    if (compile_ok) {
        ca_last_alive_idx = idx;
    } else {
        /* 编译失败 = 立即死亡 */
        r->death_cause = CA_DEATH_COMPILE_FAIL;
    }

    return idx;
}

/* ── 存活心跳 ────────────────────────────────────────────── */

void ca_tick_alive(uint32_t tick) {
    if (ca_last_alive_idx < 0 || ca_last_alive_idx >= (int)ca_hdr.count)
        return;
    ca_record_t *r = &ca_records[ca_last_alive_idx];
    if (r->death_cause != CA_DEATH_ALIVE)
        return;
    r->survival_ticks++;
    r->last_tick = tick;
    ca_dirty = 1;
}

/* ── 获取当前存活心跳数 ──────────────────────────────────── */

uint32_t ca_alive_ticks(void) {
    if (ca_last_alive_idx < 0 || ca_last_alive_idx >= (int)ca_hdr.count)
        return 0;
    if (ca_records[ca_last_alive_idx].death_cause != CA_DEATH_ALIVE)
        return 0;
    return ca_records[ca_last_alive_idx].survival_ticks;
}

/* ── 标记死亡 ────────────────────────────────────────────── */

int ca_mark_dead(int record_idx, ca_death_t cause, uint8_t fitness) {
    if (record_idx < 0 || record_idx >= (int)ca_hdr.count)
        return -1;
    ca_record_t *r = &ca_records[record_idx];
    r->death_cause = (uint8_t)cause;
    r->fitness_at_death = fitness;
    if (record_idx == ca_last_alive_idx)
        ca_last_alive_idx = -1;
    ca_hdr.modified_ns = now_ns();
    ca_dirty = 1;
    return 0;
}

/* ── 按 hash 查询 ────────────────────────────────────────────── */

int ca_query_hash(uint32_t code_hash) {
    /* 从最新往前找 */
    for (int i = (int)ca_hdr.count - 1; i >= 0; i--) {
        if (ca_records[i].code_hash == code_hash)
            return i;
    }
    return -1;
}

/* ── 按索引读取 ────────────────────────────────────────────── */

int ca_read(int idx, ca_record_t *out) {
    if (idx < 0 || idx >= (int)ca_hdr.count || !out)
        return -1;
    *out = ca_records[idx];
    return 0;
}

/* ── 按策略查询 ────────────────────────────────────────────── */

int ca_query_strategy(uint16_t strategy_id, int max_results, ca_record_t *out) {
    int found = 0;
    for (int i = (int)ca_hdr.count - 1; i >= 0 && found < max_results; i--) {
        if (ca_records[i].strategy_id == strategy_id) {
            out[found] = ca_records[i];
            found++;
        }
    }
    return found;
}

/* ── 统计 ────────────────────────────────────────────── */

void ca_stats(int *total, float *compile_rate, float *avg_survival) {
    if (total) *total = (int)ca_hdr.count;

    int compiled = 0;
    uint64_t total_survival = 0;
    int alive_count = 0;

    for (uint32_t i = 0; i < ca_hdr.count; i++) {
        if (ca_records[i].compile_ok) compiled++;
        if (ca_records[i].death_cause == CA_DEATH_ALIVE ||
            ca_records[i].survival_ticks > 0) {
            total_survival += ca_records[i].survival_ticks;
            alive_count++;
        }
    }

    if (compile_rate) {
        *compile_rate = (ca_hdr.count > 0)
            ? (float)compiled / (float)ca_hdr.count
            : 0.0f;
    }
    if (avg_survival) {
        *avg_survival = (alive_count > 0)
            ? (float)total_survival / (float)alive_count
            : 0.0f;
    }
}

/* ── 持久化 ────────────────────────────────────────────── */

int ca_save(void) {
    if (!ca_dirty) return 0;

    const char *path = CA_PATH;
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    ca_hdr.modified_ns = now_ns();

    if (write(fd, &ca_hdr, sizeof(ca_hdr)) != sizeof(ca_hdr)) {
        close(fd); unlink(tmp); return -1;
    }
    size_t data_len = ca_hdr.count * sizeof(ca_record_t);
    if (data_len > 0 && write(fd, ca_records, data_len) != (ssize_t)data_len) {
        close(fd); unlink(tmp); return -1;
    }
    fsync(fd);
    close(fd);

    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    ca_dirty = 0;
    return 0;
}

int ca_load(void) {
    int fd = open(CA_PATH, O_RDONLY);
    if (fd < 0) return -1;

    ca_header_t hdr;
    if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr) ||
        hdr.magic != CA_MAGIC || hdr.version != CA_VERSION) {
        close(fd);
        return -1;
    }

    if (hdr.count > CA_MAX_RECORDS) {
        close(fd);
        return -1;
    }

    size_t data_len = hdr.count * sizeof(ca_record_t);
    if (data_len > 0 && read(fd, ca_records, data_len) != (ssize_t)data_len) {
        close(fd);
        return -1;
    }

    ca_hdr = hdr;
    close(fd);

    /* 恢复 ca_last_alive_idx */
    ca_last_alive_idx = -1;
    for (int i = (int)ca_hdr.count - 1; i >= 0; i--) {
        if (ca_records[i].death_cause == CA_DEATH_ALIVE) {
            ca_last_alive_idx = i;
            break;
        }
    }

    ca_dirty = 0;
    return 0;
}

/* ── 清理 ────────────────────────────────────────────── */

void ca_cleanup(void) {
    if (ca_dirty) ca_save();
}
