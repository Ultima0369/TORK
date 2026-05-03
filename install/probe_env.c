/*
 * probe_env.c — TORK 环境探测模块
 * 不再假设，先摸清楚。
 * gcc -O2 -o ../build/probe_env probe_env.c -lrt
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <cpuid.h>
#include <time.h>

/* 取 CPUID 品牌字符串 */
static void get_brand(char *out, int maxlen) {
    unsigned int eax, ebx, ecx, edx, a, b, c, d;
    memset(out, 0, maxlen);
    __cpuid(0x80000000, eax, ebx, ecx, edx);
    if (eax < 0x80000004) return;
    for (int i = 0; i < 3; i++) {
        __cpuid(0x80000002 + i, a, b, c, d);
        memcpy(out + i*16,     &a, 4);
        memcpy(out + i*16 + 4, &b, 4);
        memcpy(out + i*16 + 8, &c, 4);
        memcpy(out + i*16 +12, &d, 4);
    }
    out[48] = 0;
}

/* 取 CPUID 厂商字符串 */
static void get_vendor(char *out) {
    unsigned int eax, ebx, ecx, edx;
    __cpuid(0, eax, ebx, ecx, edx);
    memcpy(out, &ebx, 4);
    memcpy(out+4, &edx, 4);
    memcpy(out+8, &ecx, 4);
    out[12] = 0;
}

static void probe_cpu_json(FILE *fp) {
    char vendor[16] = {0}, brand[64] = {0};
    unsigned int eax, ebx, ecx, edx;
    int cores = 0, threads = 0;
    int avx = 0, avx2 = 0, avx512f = 0;
    int rdrand = 0, rdseed = 0, fma = 0;
    int smap = 0, smep = 0, md_clear = 0, flush_l1d = 0;

    get_vendor(vendor);
    get_brand(brand, sizeof(brand));

    /* Feature bits: leaf 1 */
    __cpuid(1, eax, ebx, ecx, edx);
    avx    = (ecx >> 28) & 1;
    fma    = (ecx >> 12) & 1;
    rdrand = (ecx >> 30) & 1;
    threads = (ebx >> 16) & 0xff;  /* logical threads from APIC ID */

    /* Feature bits: leaf 7 */
    __cpuid_count(7, 0, eax, ebx, ecx, edx);
    avx2     = (ebx >> 5) & 1;
    avx512f  = (ebx >> 16) & 1;
    rdseed   = (ebx >> 18) & 1;
    smap     = (ebx >> 20) & 1;
    smep     = (ebx >> 7) & 1;
    md_clear = (edx >> 10) & 1;
    flush_l1d = (edx >> 28) & 1;

    /* Physical cores: leaf 4 */
    cores = threads; /* fallback */
    int max_leaf = eax; /* eax from CPUID 0 */
    for (int i = 0; ; i++) {
        __cpuid_count(4, i, eax, ebx, ecx, edx);
        int type = eax & 0x1f;
        if (type == 0) break;
        if (type == 1) { cores = ((eax >> 26) & 0x3f) + 1; }
    }

    /* Max frequency from sysfs */
    double max_mhz = 0;
    FILE *f = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
    if (f) { int khz; if (fscanf(f, "%d", &khz) == 1) max_mhz = khz / 1000.0; fclose(f); }

    fprintf(fp, "\"cpu\": {\n");
    fprintf(fp, "  \"vendor\": \"%s\",\n", vendor);
    fprintf(fp, "  \"brand\": \"%s\",\n", brand);
    fprintf(fp, "  \"physical_cores\": %d,\n", cores);
    fprintf(fp, "  \"logical_threads\": %d,\n", threads);
    fprintf(fp, "  \"max_mhz\": %.1f,\n", max_mhz);
    fprintf(fp, "  \"features\": {\n");
    fprintf(fp, "    \"avx\": %d,\n", avx);
    fprintf(fp, "    \"avx2\": %d,\n", avx2);
    fprintf(fp, "    \"avx512f\": %d,\n", avx512f);
    fprintf(fp, "    \"fma\": %d,\n", fma);
    fprintf(fp, "    \"rdrand\": %d,\n", rdrand);
    fprintf(fp, "    \"rdseed\": %d,\n", rdseed);
    fprintf(fp, "    \"smap\": %d,\n", smap);
    fprintf(fp, "    \"smep\": %d,\n", smep);
    fprintf(fp, "    \"md_clear\": %d,\n", md_clear);
    fprintf(fp, "    \"flush_l1d\": %d\n", flush_l1d);
    fprintf(fp, "  }\n");
    fprintf(fp, "}");
}

static void probe_os_json(FILE *fp) {
    struct utsname uts;
    uname(&uts);
    char os_name[128] = "unknown", os_ver[64] = "";
    FILE *f = fopen("/etc/os-release", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "PRETTY_NAME=\"%127[^\"]\"", os_name) == 1) continue;
            if (sscanf(line, "VERSION_ID=\"%63[^\"]\"", os_ver) == 1) continue;
        }
        fclose(f);
    }
    fprintf(fp, "\"os\": {\n");
    fprintf(fp, "  \"name\": \"%s\",\n", os_name);
    fprintf(fp, "  \"version\": \"%s\",\n", os_ver);
    fprintf(fp, "  \"kernel\": \"%s\",\n", uts.release);
    fprintf(fp, "  \"arch\": \"%s\",\n", uts.machine);
    fprintf(fp, "  \"hostname\": \"%s\"\n", uts.nodename);
    fprintf(fp, "}");
}

static void probe_memory_json(FILE *fp) {
    struct sysinfo si;
    sysinfo(&si);
    fprintf(fp, "\"memory\": {\n");
    fprintf(fp, "  \"total_mb\": %lu,\n", si.totalram / (1024*1024 / si.mem_unit));
    fprintf(fp, "  \"free_mb\": %lu,\n", si.freeram / (1024*1024 / si.mem_unit));
    fprintf(fp, "  \"swap_total_mb\": %lu,\n", si.totalswap / (1024*1024 / si.mem_unit));
    fprintf(fp, "  \"swap_free_mb\": %lu\n", si.freeswap / (1024*1024 / si.mem_unit));
    fprintf(fp, "}");
}

static void probe_disk_json(FILE *fp) {
    struct statvfs vfs;
    if (statvfs("/", &vfs) == 0) {
        unsigned long total = (unsigned long)(vfs.f_frsize * vfs.f_blocks / (1024UL*1024));
        unsigned long free  = (unsigned long)(vfs.f_frsize * vfs.f_bfree  / (1024UL*1024));
        fprintf(fp, "\"disk\": {\n");
        fprintf(fp, "  \"total_mb\": %lu,\n", total);
        fprintf(fp, "  \"free_mb\": %lu,\n", free);
        fprintf(fp, "  \"used_pct\": %.1f\n", 100.0 * (1 - (double)vfs.f_bfree / vfs.f_blocks));
        fprintf(fp, "}");
    }
}

static void probe_toolchain_json(FILE *fp) {
    fprintf(fp, "\"toolchain\": {\n");
    FILE *f;
    char buf[256];
    const char *cmds[] = {
        "gcc --version 2>/dev/null | head -1",
        "python3 --version 2>/dev/null",
        "as --version 2>/dev/null | head -1",
        "make --version 2>/dev/null | head -1"
    };
    const char *keys[] = {"gcc", "python3", "as", "make"};
    for (int i = 0; i < 4; i++) {
        f = popen(cmds[i], "r");
        buf[0] = 0;
        if (f) {
            if (fgets(buf, sizeof(buf), f)) buf[strcspn(buf, "\n")] = 0;
            pclose(f);
        }
        if (!buf[0]) snprintf(buf, sizeof(buf), "unknown");
        fprintf(fp, "  \"%s\": \"%s\"%s\n", keys[i], buf, i < 3 ? "," : "");
    }
    fprintf(fp, "}");
}

int main(int argc, char **argv) {
    const char *outpath = NULL;
    if (argc > 1) outpath = argv[1];
    FILE *fp = stdout;
    if (outpath) { fp = fopen(outpath, "w"); if (!fp) { perror("fopen"); return 1; } }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"probe_time\": %ld,\n", (long)time(NULL));
    probe_cpu_json(fp);     fprintf(fp, ",\n");
    probe_os_json(fp);      fprintf(fp, ",\n");
    probe_memory_json(fp);  fprintf(fp, ",\n");
    probe_disk_json(fp);    fprintf(fp, ",\n");
    probe_toolchain_json(fp);
    fprintf(fp, "\n}\n");
    if (outpath) fclose(fp);
    return 0;
}
