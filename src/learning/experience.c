#include "experience.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

/* ── Global ring buffer ───────────────────────────────────── */
static experience_buffer_t g_buf;
static int g_initialized = 0;

/* ── 精英保留: 更新 top 10% outcome 阈值 ── */
static void update_elite_threshold(void) {
    if (g_buf.count < 32) return;
    int n = (g_buf.count > EXP_MAX_EXPERIENCES) ? EXP_MAX_EXPERIENCES : g_buf.count;
    /* 收集所有 outcome，找 90th percentile */
    int8_t outcomes[EXP_MAX_EXPERIENCES];
    for (int i = 0; i < n; i++)
        outcomes[i] = g_buf.slots[i].outcome;
    /* 简单排序 (n≤256) */
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (outcomes[j] > outcomes[i]) {
                int8_t tmp = outcomes[i];
                outcomes[i] = outcomes[j];
                outcomes[j] = tmp;
            }
    g_buf.elite_threshold = outcomes[n / 10];
}

/* ── Init: load from disk or start fresh ──────────────────── */
void exp_init(void) {
    memset(&g_buf, 0, sizeof(g_buf));
    
    FILE *f = fopen(EXP_PATH, "rb");
    if (f) {
        size_t got = fread(&g_buf, 1, sizeof(g_buf), f);
        fclose(f);
        if (got == sizeof(g_buf) && g_buf.count > 0) {
            /* Validate head/count to prevent out-of-bounds from corrupted data */
            if (g_buf.head >= EXP_MAX_EXPERIENCES) g_buf.head = 0;
            if (g_buf.count > EXP_MAX_EXPERIENCES) g_buf.count = 0;
            if (g_buf.count == 0) goto fresh;
            printf("  EXP: loaded %u experiences from %s\n", g_buf.count, EXP_PATH);
            g_initialized = 1;
            return;
        }
    }

    /* Fresh start */
fresh:
    g_buf.head = 0;
    g_buf.count = 0;
    g_initialized = 1;
    printf("  EXP: fresh start, no prior experience\n");
}

/* ── Save to disk ─────────────────────────────────────────── */
void exp_save(void) {
    if (!g_initialized) return;
    
    /* Ensure directory exists */
    FILE *f = fopen(EXP_PATH, "wb");
    if (!f) {
        /* Try to create persist directory (fork+execl, no shell) */
        pid_t mk = fork();
        if (mk == 0) { execl("/bin/mkdir", "mkdir", "-p", "persist", NULL); _exit(1); }
        if (mk > 0) { int st; waitpid(mk, &st, 0); }
        f = fopen(EXP_PATH, "wb");
    }
    if (f) {
        size_t wrote = fwrite(&g_buf, 1, sizeof(g_buf), f);
        fclose(f);
        if (wrote == sizeof(g_buf))
            printf("  EXP: saved %u experiences\n", g_buf.count);
        else
            fprintf(stderr, "  EXP: save failed (wrote %zu/%zu)\n", wrote, sizeof(g_buf));
    } else {
        fprintf(stderr, "  EXP: cannot write %s\n", EXP_PATH);
    }
}

/* ── Record a new experience (with elite preservation) ─────── */
void exp_record(uint64_t tick, uint8_t hw_stress, int8_t drive_pre,
                uint16_t gen_count, uint8_t action_type, int8_t action_param,
                int8_t outcome, uint8_t crash, uint8_t compile_ok,
                uint8_t hw_stress_post, int8_t drive_post) {
    if (!g_initialized) exp_init();

    /* 精英保留: 缓冲区满时，保护高 outcome 经验不被覆盖 */
    if (g_buf.count >= EXP_MAX_EXPERIENCES && g_buf.elite_threshold != 0) {
        int8_t victim_outcome = g_buf.slots[g_buf.head].outcome;
        /* 如果即将被覆盖的是精英，而新经验不是精英，找非精英槽位 */
        if (victim_outcome >= g_buf.elite_threshold && outcome < g_buf.elite_threshold) {
            int start = g_buf.head;
            int i = start;
            int found = 0;
            do {
                if (g_buf.slots[i].outcome < g_buf.elite_threshold) {
                    g_buf.head = i;
                    found = 1;
                    break;
                }
                i = (i + 1) % EXP_MAX_EXPERIENCES;
            } while (i != start);
            if (!found) g_buf.head = start;  /* 全是精英，强制覆盖 */
        }
    }

    experience_t *e = &g_buf.slots[g_buf.head];
    
    /* Fill in the experience */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    e->tick          = tick;
    e->timestamp_ns  = (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
    e->hw_stress     = hw_stress;
    e->drive_pre     = drive_pre;
    e->gen_count     = gen_count;
    e->action_type   = action_type;
    e->action_param  = action_param;
    e->outcome       = outcome;
    e->crash_occurred = crash;
    e->compile_ok    = compile_ok;
    e->hw_stress_post = hw_stress_post;
    e->drive_post    = drive_post;
    memset(e->_pad, 0, sizeof(e->_pad));
    
    /* Advance head (circular) */
    g_buf.head = (g_buf.head + 1) % EXP_MAX_EXPERIENCES;
    if (g_buf.count < EXP_MAX_EXPERIENCES) g_buf.count++;

    /* 每 32 次写入更新精英阈值 */
    if (g_buf.count >= 32 && (g_buf.head % 32 == 0))
        update_elite_threshold();

    /* Auto-save every 10 experiences */
    if (g_buf.count % 10 == 0)
        exp_save();
}

/* ── Get recent N experiences ─────────────────────────────── */
int exp_recent(int n, experience_t *out) {
    if (!g_initialized || g_buf.count == 0 || n <= 0) return 0;
    
    int actual = (n > (int)g_buf.count) ? (int)g_buf.count : n;
    int tail = g_buf.head;
    
    /* Walk backwards from head */
    for (int i = 0; i < actual; i++) {
        tail = (tail - 1 + EXP_MAX_EXPERIENCES) % EXP_MAX_EXPERIENCES;
        out[i] = g_buf.slots[tail];
    }
    
    return actual;
}

/* ── 按索引读取经验 ──
 * idx=0 最旧, idx=count-1 最新
 * ─────────────────────────────────────────── */
int exp_read(int idx, experience_t *out) {
    if (!g_initialized || !out) return -1;
    if (idx < 0 || (uint32_t)idx >= g_buf.count) return -1;
    
    uint32_t phys_idx;
    if (g_buf.count < EXP_MAX_EXPERIENCES) {
        phys_idx = idx;
    } else {
        phys_idx = (g_buf.head + idx) % EXP_MAX_EXPERIENCES;
    }
    
    memcpy(out, &g_buf.slots[phys_idx], sizeof(experience_t));
    return 0;
}

/* ── Filter by action type ────────────────────────────────── */
int exp_filter(uint8_t action_type, int max_results, experience_t *out) {
    if (!g_initialized || g_buf.count == 0 || max_results <= 0) return 0;
    
    int found = 0;
    int idx = g_buf.head;
    int checked = 0;
    int total = (g_buf.count < EXP_MAX_EXPERIENCES) ? g_buf.count : EXP_MAX_EXPERIENCES;
    
    while (checked < total && found < max_results) {
        idx = (idx - 1 + EXP_MAX_EXPERIENCES) % EXP_MAX_EXPERIENCES;
        if (g_buf.slots[idx].action_type == action_type) {
            out[found++] = g_buf.slots[idx];
        }
        checked++;
    }
    
    return found;
}

/* ── Total count ──────────────────────────────────────────── */
uint32_t exp_count(void) {
    return g_initialized ? g_buf.count : 0;
}

/* ── Success rate for an action type ──────────────────────── */
float exp_success_rate(uint8_t action_type) {
    if (!g_initialized || g_buf.count == 0) return -1.0f;
    
    int successes = 0;
    int total = 0;
    int idx = g_buf.head;
    int checked = 0;
    int max_check = (g_buf.count < 1000) ? g_buf.count : 1000; /* Last 1000 */
    
    while (checked < max_check) {
        idx = (idx - 1 + EXP_MAX_EXPERIENCES) % EXP_MAX_EXPERIENCES;
        if (g_buf.slots[idx].action_type == action_type) {
            total++;
            if (g_buf.slots[idx].outcome > 0 && !g_buf.slots[idx].crash_occurred)
                successes++;
        }
        checked++;
    }
    
    return (total > 0) ? ((float)successes / total) : -1.0f;
}

/* ── Update last experience outcome ────────────────────────── */
void exp_update_last(int8_t outcome, uint8_t crash, uint8_t compile_ok,
                     uint8_t hw_stress_post, int8_t drive_post) {
    if (!g_initialized || g_buf.count == 0) return;
    
    /* Find the most recent (last written) experience */
    int last_idx = (g_buf.head - 1 + EXP_MAX_EXPERIENCES) % EXP_MAX_EXPERIENCES;
    experience_t *e = &g_buf.slots[last_idx];
    
    e->outcome = outcome;
    e->crash_occurred = crash;
    e->compile_ok = compile_ok;
    e->hw_stress_post = hw_stress_post;
    e->drive_post = drive_post;
}

/* ── Get last experience ───────────────────────────────────── */
const experience_t *exp_last(void) {
    if (!g_initialized || g_buf.count == 0) return NULL;
    int last_idx = (g_buf.head - 1 + EXP_MAX_EXPERIENCES) % EXP_MAX_EXPERIENCES;
    return &g_buf.slots[last_idx];
}
