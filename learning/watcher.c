#include "watcher.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>

static watcher_t g_watcher;
static int g_initialized = 0;

/* ── 初始化 ──────────────────────────────────────────────── */
void watcher_init(void) {
    memset(&g_watcher, 0, sizeof(g_watcher));
    g_watcher.idle_threshold = 500;  /* 500 ticks of inactivity = idle */
    g_watcher.last_active_tick = 0;
    g_watcher.is_active = 1;
    g_initialized = 1;
    printf("  WATCH: watcher initialized (%d event ring)\n", WATCH_MAX_EVENTS);
}

/* ── 记录事件 ────────────────────────────────────────────── */
void watcher_record(watch_event_type_t type, uint32_t pid,
                    const char *brief, int8_t importance) {
    if (!g_initialized) return;
    
    watch_event_t *e = &g_watcher.ring[g_watcher.head];
    e->tick = g_watcher.count;  /* Use count as synthetic tick */
    e->type = type;
    e->pid = pid;
    e->duration_ticks = 0;
    e->importance = importance;
    e->category = 0;
    snprintf(e->brief, sizeof(e->brief), "%s", brief ? brief : "");
    
    g_watcher.head = (g_watcher.head + 1) % WATCH_MAX_EVENTS;
    g_watcher.count++;
    
    /* Update stats */
    switch (type) {
        case WATCH_EVENT_COMPILE:   g_watcher.total_compilations++; break;
        case WATCH_EVENT_ERROR:     g_watcher.total_errors++; break;
        case WATCH_EVENT_FILE_SAVE: g_watcher.total_saves++; break;
        default: break;
    }
    
    g_watcher.last_active_tick = g_watcher.count;
    g_watcher.is_active = 1;
}

/* ── 扫描 /proc 检测用户活动 ────────────────────────────── */
void watcher_scan_proc(void) {
    if (!g_initialized) return;
    
    /* Check for user-facing processes (editor, terminal, browser) */
    DIR *proc = opendir("/proc");
    if (!proc) return;
    
    int user_procs = 0;
    struct dirent *entry;
    
    while ((entry = readdir(proc)) != NULL) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        
        char cmdline[512];
        snprintf(cmdline, sizeof(cmdline), "/proc/%s/comm", entry->d_name);
        
        FILE *f = fopen(cmdline, "r");
        if (f) {
            char comm[64] = {0};
            if (fgets(comm, sizeof(comm), f)) {
                /* Detect known user tools */
                if (strstr(comm, "code") || strstr(comm, "vim") || 
                    strstr(comm, "nvim") || strstr(comm, "emacs") ||
                    strstr(comm, "terminal") || strstr(comm, "bash") ||
                    strstr(comm, "zsh") || strstr(comm, "firefox") ||
                    strstr(comm, "chrome") || strstr(comm, "gedit") ||
                    strstr(comm, "subl") || strstr(comm, "claude") ||
                    strstr(comm, "chatbox")) {
                    user_procs++;
                }
                /* Detect compilation activity */
                if (strstr(comm, "gcc") || strstr(comm, "g++") ||
                    strstr(comm, "make") || strstr(comm, "clang") ||
                    strstr(comm, "cargo") || strstr(comm, "go")) {
                    watcher_record(WATCH_EVENT_COMPILE, atoi(entry->d_name), comm, 5);
                }
            }
            fclose(f);
        }
    }
    closedir(proc);
    
    /* Update activity status */
    if (user_procs > 0) {
        g_watcher.last_active_tick = g_watcher.count;
        g_watcher.is_active = 1;
    } else if (g_watcher.count - g_watcher.last_active_tick > g_watcher.idle_threshold) {
        if (g_watcher.is_active) {
            watcher_record(WATCH_EVENT_USER_IDLE, 0, "user idle", 1);
        }
        g_watcher.is_active = 0;
    }
}

/* ── 学习模式 ────────────────────────────────────────────── */
int watcher_learn_patterns(void) {
    if (!g_initialized || g_watcher.count < 10) return 0;
    
    int found = 0;
    
    /* Simple pattern: look for frequently co-occurring event pairs */
    for (uint32_t i = 0; i < WATCH_MAX_EVENTS - 1 && i < g_watcher.count - 1; i++) {
        uint32_t idx = (g_watcher.head - 2 - i + WATCH_MAX_EVENTS) % WATCH_MAX_EVENTS;
        uint32_t next = (idx + 1) % WATCH_MAX_EVENTS;
        
        watch_event_t *cur = &g_watcher.ring[idx];
        watch_event_t *nxt = &g_watcher.ring[next];
        
        if (cur->type == WATCH_EVENT_NONE || nxt->type == WATCH_EVENT_NONE) continue;
        
        /* Check if this pair already has a pattern */
        int found_pattern = 0;
        for (uint32_t p = 0; p < g_watcher.pattern_count; p++) {
            if (g_watcher.patterns[p].trigger_event == cur->type &&
                g_watcher.patterns[p].follow_event == nxt->type) {
                g_watcher.patterns[p].count++;
                g_watcher.patterns[p].confidence = 
                    (float)g_watcher.patterns[p].count / (float)g_watcher.count;
                found_pattern = 1;
                break;
            }
        }
        
        /* New pattern */
        if (!found_pattern && g_watcher.pattern_count < WATCH_MAX_PATTERNS) {
            watch_pattern_t *p = &g_watcher.patterns[g_watcher.pattern_count++];
            p->id = g_watcher.pattern_count;
            p->trigger_event = cur->type;
            p->follow_event = nxt->type;
            p->typical_gap = 1;
            p->count = 1;
            p->confidence = 1.0f / (float)g_watcher.count;
            snprintf(p->description, sizeof(p->description), 
                     "event_%d → event_%d", cur->type, nxt->type);
            found++;
        }
    }
    
    return found;
}

/* ── 观察摘要 ────────────────────────────────────────────── */
void watcher_summary(char *buf, int buf_size) {
    if (!g_initialized || !buf || buf_size < 1) return;
    
    char tmp[512];
    snprintf(tmp, sizeof(tmp),
        "WATCH: events=%u active=%d compilations=%u errors=%u saves=%u patterns=%u",
        g_watcher.count, g_watcher.is_active, 
        g_watcher.total_compilations, g_watcher.total_errors, g_watcher.total_saves,
        g_watcher.pattern_count);
    
    snprintf(buf, buf_size, "%s", tmp);
}

/* ── 保存 ────────────────────────────────────────────────── */
int watcher_save(void) {
    if (!g_initialized) return -1;
    FILE *f = fopen("persist/watcher.bin", "wb");
    if (!f) return -1;
    fwrite(&g_watcher, sizeof(g_watcher), 1, f);
    fclose(f);
    return 0;
}

/* ── 加载 ────────────────────────────────────────────────── */
int watcher_load(void) {
    if (!g_initialized) watcher_init();
    FILE *f = fopen("persist/watcher.bin", "rb");
    if (!f) return -1;
    fread(&g_watcher, sizeof(g_watcher), 1, f);
    fclose(f);
    printf("  WATCH: loaded %u events, %u patterns from disk\n", 
           g_watcher.count, g_watcher.pattern_count);
    return 0;
}

/* ── 获取状态指针 ────────────────────────────────────────── */
watcher_t* watcher_get_state(void) {
    if (!g_initialized) return NULL;
    return &g_watcher;
}
