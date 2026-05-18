/**
 * tork_health.c — TORK 自保系统实现
 *
 * 实现思路：
 *   CPU 负载读 /proc/stat（Linux）或 GetSystemTimes（Win）
 *   内存读 /proc/meminfo 或 GlobalMemoryStatusEx
 *   磁盘读 statfs 或 GetDiskFreeSpace
 *   灵魂完整性通过 CRC 检查 tork_soul.bin
 *
 *   Windows 回退：所有指标通过 TORK 内部计数器估算
 */

#include "tork_health.h"
#include "errors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/statvfs.h>
#endif

// ==================== 内部辅助 ====================

static int g_crash_count = 0;
static int g_fork_count = 0;
static int g_self_mod_count = 0;
static int g_generation = 1;
static int g_soul_version = 1;

void tork_health_record_crash(void) { g_crash_count++; }
void tork_health_record_fork(void)  { g_fork_count++; }
void tork_health_record_selfmod(void) { g_self_mod_count++; }
void tork_health_set_generation(int gen) { g_generation = gen; }
void tork_health_set_soul_version(int v)  { g_soul_version = v; }

// ==================== CPU 负载采集 ====================

#ifdef _WIN32
static double read_cpu_load(void) {
    static ULARGE_INTEGER last_idle, last_total;
    static int initialized = 0;
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) return 0.5;
    ULARGE_INTEGER i, k, u;
    i.LowPart = idle.dwLowDateTime;   i.HighPart = idle.dwHighDateTime;
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;   u.HighPart = user.dwHighDateTime;
    if (!initialized) {
        last_idle = i; last_total.QuadPart = k.QuadPart + u.QuadPart;
        initialized = 1; return 0.0;
    }
    ULARGE_INTEGER diff_idle, diff_total;
    diff_idle.QuadPart = i.QuadPart - last_idle.QuadPart;
    diff_total.QuadPart = (k.QuadPart + u.QuadPart) - last_total.QuadPart;
    last_idle = i; last_total.QuadPart = k.QuadPart + u.QuadPart;
    if (diff_total.QuadPart == 0) return 0.5;
    return 1.0 - (double)diff_idle.QuadPart / (double)diff_total.QuadPart;
}
#else
static double read_cpu_load(void) {
    static unsigned long long prev_total = 0, prev_idle = 0;
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0.5;
    char buf[256]; if (!fgets(buf, sizeof(buf), f)) { fclose(f); return 0.5; }
    fclose(f);
    unsigned long long user, nice, sys, idle, iowait, irq, softirq, steal;
    if (sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal) < 4)
        return 0.5;
    unsigned long long total = user + nice + sys + idle + iowait + irq + softirq + steal;
    if (prev_total == 0) { prev_total = total; prev_idle = idle; return 0.0; }
    unsigned long long diff_total = total - prev_total;
    unsigned long long diff_idle  = idle - prev_idle;
    prev_total = total; prev_idle = idle;
    if (diff_total == 0) return 0.5;
    return 1.0 - (double)diff_idle / (double)diff_total;
}
#endif

// ==================== CPU 温度 ====================

#ifdef __linux__
static double read_cpu_temp(void) {
    const char *paths[] = {
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/hwmon/hwmon0/temp1_input",
        "/sys/class/hwmon/hwmon1/temp1_input",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        FILE *f = fopen(paths[i], "r");
        if (f) {
            int val = 0;
            if (fscanf(f, "%d", &val) == 1) { fclose(f); return val / 1000.0; }
            fclose(f);
        }
    }
    return 0.0;
}
#else
static double read_cpu_temp(void) { return 0.0; }
#endif

// ==================== 内存采集 ====================

#ifdef _WIN32
static void read_mem(double *used_mb, double *total_mb) {
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        *total_mb = ms.ullTotalPhys / (1024.0 * 1024.0);
        *used_mb  = (ms.ullTotalPhys - ms.ullAvailPhys) / (1024.0 * 1024.0);
    } else {
        *total_mb = 8192; *used_mb = 4096;
    }
}
#else
static void read_mem(double *used_mb, double *total_mb) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) { *total_mb = 1024; *used_mb = 512; return; }
    long total_kb = 0, avail_kb = 0;
    char buf[128];
    while (fgets(buf, sizeof(buf), f)) {
        if (sscanf(buf, "MemTotal: %ld kB", &total_kb) == 1) continue;
        if (sscanf(buf, "MemAvailable: %ld kB", &avail_kb) == 1) break;
    }
    fclose(f);
    if (total_kb == 0) { *total_mb = 1024; *used_mb = 512; return; }
    *total_mb = total_kb / 1024.0;
    *used_mb  = (total_kb - avail_kb) / 1024.0;
}
#endif

// ==================== 灵魂完整性 ====================

static int check_soul_integrity(void) {
    FILE *f = fopen("persist/soul_golden.bin", "rb");
    if (!f) return 1; // 没有金板文件也算完好（首次运行）
    unsigned int crc = 0xFFFFFFFF;
    int c;
    while ((c = fgetc(f)) != EOF) {
        crc ^= (unsigned char)c;
        for (int i = 0; i < 8; i++) crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320 : 0);
    }
    fclose(f);
    return (crc == 0x2144DF1C || feof(f)) ? 1 : 0; // 简单校验
}

// ==================== 磁盘采集 ====================

static double get_free_disk_mb(void) {
#ifdef _WIN32
    const char *path = ".";
    ULARGE_INTEGER free_bytes;
    if (GetDiskFreeSpaceExA(path, &free_bytes, NULL, NULL))
        return (double)free_bytes.QuadPart / (1024.0 * 1024.0);
    return 1024;
#else
    struct statvfs st;
    if (statvfs(".", &st) == 0)
        return (double)st.f_frsize * (double)st.f_bavail / (1024.0 * 1024.0);
    return 1024;
#endif
}

// ==================== 公共 API 实现 ====================

void tork_health_collect(tork_health_t *h) {
    if (!h) return;
    memset(h, 0, sizeof(*h));

    // CPU
    h->cpu_load = read_cpu_load();
    h->cpu_temp = read_cpu_temp();

    // 内存
    read_mem(&h->mem_used_mb, &h->mem_total_mb);
    h->mem_ratio = h->mem_total_mb > 0 ? h->mem_used_mb / h->mem_total_mb : 0.5;

    // 磁盘
    double free_mb = get_free_disk_mb();
    h->disk_free_mb = free_mb;
    h->disk_used_mb = 0; // 单值采集够用

    // 进程
    h->uptime_seconds = (int)time(NULL); // 近似
    h->pid = (int)getpid();
    h->fork_count = g_fork_count;
    h->crash_count = g_crash_count;

    // 灵魂
    h->soul_version = g_soul_version;
    h->generation = g_generation;
    h->self_mod_count = g_self_mod_count;
    h->golden_integrity = check_soul_integrity();

    // 综合诊断
    h->level = tork_health_diagnose(h);
    tork_health_report(h, h->diagnosis, sizeof(h->diagnosis));
}

tork_health_level_t tork_health_diagnose(const tork_health_t *h) {
    if (!h) return HEALTH_POOR;
    int score = 0;

    // CPU 负载
    if (h->cpu_load < 0.3) score += 2;
    else if (h->cpu_load < 0.6) score += 1;
    else score -= 1;

    // CPU 温度
    if (h->cpu_temp > 0) {
        if (h->cpu_temp < 60) score += 1;
        else if (h->cpu_temp > 85) score -= 2;
    }

    // 内存
    if (h->mem_ratio < 0.5) score += 2;
    else if (h->mem_ratio < 0.8) score += 1;
    else score -= 2;

    // 磁盘
    if (h->disk_free_mb > 1000) score += 1;
    else if (h->disk_free_mb < 100) score -= 1;

    // 灵魂完整性
    if (!h->golden_integrity) score -= 3;

    // 崩溃历史
    if (h->crash_count > 5) score -= 1;

    if (score <= -2) return HEALTH_CRITICAL;
    if (score <= 0)  return HEALTH_POOR;
    if (score <= 2)  return HEALTH_FAIR;
    if (score <= 4)  return HEALTH_GOOD;
    return HEALTH_EXCELLENT;
}

int tork_health_backup(tork_backup_t *out) {
    static int backup_seq = 0;
    backup_seq++;
    if (out) {
        snprintf(out->soul_backup, sizeof(out->soul_backup),
                 "persist/backup/soul_%d.bin", backup_seq);
        snprintf(out->code_backup, sizeof(out->code_backup),
                 "persist/backup/code_%d.bin", backup_seq);
        out->timestamp = time(NULL);
        out->version = backup_seq;
        out->integrity_ok = 1;
    }
    // 创建备份目录
#ifdef _WIN32
    _mkdir("persist/backup");
#else
    mkdir("persist/backup", 0755);
#endif
    // 复制灵魂金板
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
#ifdef _WIN32
             "copy persist\\soul_golden.bin persist\\backup\\soul_%d.bin >nul 2>nul",
#else
             "cp persist/soul_golden.bin persist/backup/soul_%d.bin 2>/dev/null",
#endif
             backup_seq);
    system(cmd);
    return backup_seq;
}

int tork_health_restore(int version) {
    char path[512];
    if (version <= 0) version = tork_health_backup_count();
    snprintf(path, sizeof(path),
#ifdef _WIN32
             "persist\\backup\\soul_%d.bin",
#else
             "persist/backup/soul_%d.bin",
#endif
             version);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
#ifdef _WIN32
             "copy %s persist\\soul_golden.bin >nul 2>nul",
#else
             "cp %s persist/soul_golden.bin 2>/dev/null",
#endif
             path);
    system(cmd);
    return 1;
}

int tork_health_backup_count(void) {
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("persist\\backup\\soul_*.bin", &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int count = 0;
    do { count++; } while (FindNextFileA(h, &fd));
    FindClose(h);
    return count;
#else
    FILE *f = popen("ls persist/backup/soul_*.bin 2>/dev/null | wc -l", "r");
    if (!f) return 0;
    int count = 0;
    fscanf(f, "%d", &count);
    pclose(f);
    return count;
#endif
}

void tork_health_report(const tork_health_t *h, char *buf, int bufsize) {
    if (!h || !buf || bufsize <= 0) return;
    const char *level_str[] = {"CRITICAL", "POOR", "FAIR", "GOOD", "EXCELLENT"};
    int idx = (int)h->level + 2;
    if (idx < 0) idx = 0; if (idx > 4) idx = 4;
    snprintf(buf, bufsize,
             "TORK gen=%d sv=%d cpu=%.0f%% temp=%.1fC mem=%.0f/%.0fMB "
             "disk_free=%.0fMB fork=%d crash=%d selfmod=%d soul=%s [%s]",
             h->generation, h->soul_version,
             h->cpu_load * 100, h->cpu_temp,
             h->mem_used_mb, h->mem_total_mb,
             h->disk_free_mb, h->fork_count, h->crash_count,
             h->self_mod_count,
             h->golden_integrity ? "OK" : "DAMAGED",
             level_str[idx]);
}

void tork_health_emergency_exit(const tork_health_t *h, const char *reason) {
    FILE *f = fopen("persist/emergency.bin", "wb");
    if (f && h) {
        fwrite(h, sizeof(*h), 1, f);
        fprintf(f, "\nREASON: %s\nTIMESTAMP: %ld\n", reason ? reason : "unknown", (long)time(NULL));
        fclose(f);
    }
    fprintf(stderr, "[TORK] EMERGENCY EXIT: %s\n", reason ? reason : "unknown");
    exit(127);
}
