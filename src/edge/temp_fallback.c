/* ══════════════════════════════════════════════════════════════
 * TORK 温度感知回退层
 *
 * tork_core.asm 直接从 /sys/class/thermal/thermal_zone0/temp 读取温度。
 * 在 Docker 容器、VM、WSL 中此路径不存在。
 *
 * 此模块提供多路径回退 + 模拟温度生成，写入 Soul 的 S_TEMP_SENSOR 字段。
 * 汇编核心通过 Soul 读取温度，无需修改 asm 代码。
 * ══════════════════════════════════════════════════════════════ */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

/* ── 温度源优先级 ──────────────────────────────────────── */
typedef enum {
    TEMP_SRC_SYSFS  = 0,  /* /sys/class/thermal/thermal_zoneN/temp */
    TEMP_SRC_CPU    = 1,  /* /sys/class/thermal/cpu_thermal/temp (AArch64) */
    TEMP_SRC_ACPI   = 2,  /* /sys/class/thermal/acpitz/temp */
    TEMP_SRC_HWMON  = 3,  /* /sys/class/hwmon/hwmon*/temp*_input */
    TEMP_SRC_THERM  = 4,  /* /sys/class/thermal/thermal_zone*/temp (通配) */
    TEMP_SRC_SIM    = 5,  /* 模拟 (容器/VM 回退) */
    TEMP_SRC_NONE   = 6,  /* 无源 */
} temp_source_t;

static const char *temp_paths[] = {
    "/sys/class/thermal/thermal_zone0/temp",
    "/sys/class/thermal/cpu_thermal/temp",
    "/sys/class/thermal/acpitz/temp",
    "/sys/class/hwmon/hwmon1/temp1_input",
    "/sys/class/hwmon/hwmon0/temp1_input",
    NULL
};

static temp_source_t g_temp_source = TEMP_SRC_NONE;
static int g_temp_mock = 45;  /* 默认模拟温度 45°C */

/* ── 探测可用温度源 ────────────────────────────────────── */
temp_source_t temp_probe(void) {
    /* 先尝试已知路径 */
    for (int i = TEMP_SRC_SYSFS; i <= TEMP_SRC_HWMON; i++) {
        int fd = open(temp_paths[i], O_RDONLY);
        if (fd >= 0) {
            close(fd);
            g_temp_source = (temp_source_t)i;
            printf("  TEMP: source found at %s\n", temp_paths[i]);
            return g_temp_source;
        }
    }

    /* 尝试通配: thermal_zone1, thermal_zone2... */
    char path[64];
    for (int i = 1; i < 10; i++) {
        snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", i);
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            close(fd);
            g_temp_source = TEMP_SRC_THERM;
            printf("  TEMP: source found at %s\n", path);
            return g_temp_source;
        }
    }

    /* 全无 → 模拟模式 */
    g_temp_source = TEMP_SRC_SIM;
    printf("  TEMP: no hardware source found, using simulated (%d°C)\n", g_temp_mock);
    return g_temp_source;
}

/* ── 读取温度 (毫摄氏度, 与汇编内核兼容) ──────────────── */
int temp_read_millic(void) {
    if (g_temp_source == TEMP_SRC_NONE)
        temp_probe();

    if (g_temp_source <= TEMP_SRC_THERM) {
        /* 真实传感器读取 */
        const char *path = (g_temp_source <= TEMP_SRC_HWMON)
            ? temp_paths[g_temp_source]
            : NULL;

        char buf[16];
        int fd;

        if (path) {
            fd = open(path, O_RDONLY);
        } else {
            /* thermal_zone 通配: 重新扫描 */
            char p[64];
            for (int i = 0; i < 10; i++) {
                snprintf(p, sizeof(p), "/sys/class/thermal/thermal_zone%d/temp", i);
                fd = open(p, O_RDONLY);
                if (fd >= 0) break;
            }
            if (fd < 0) goto fallback;
        }

        if (fd < 0) goto fallback;

        int n = (int)read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            int val = atoi(buf);
            if (val > 0) return val;
        }
    }

fallback:
    /* 模拟温度: 在基础值上做随机波动 (±3°C) */
    if (g_temp_source == TEMP_SRC_SIM) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        int noise = (int)(ts.tv_nsec % 6000) - 3000;  /* ±3°C */
        return (g_temp_mock * 1000) + noise;
    }

    return 45000;  /* 最后兜底: 45°C */
}

/* ── 设置模拟温度 (用于测试/容器) ──────────────────────── */
void temp_set_mock(int celsius) {
    g_temp_mock = celsius;
    if (g_temp_source == TEMP_SRC_SIM)
        printf("  TEMP: mock set to %d°C\n", celsius);
}

/* ── 获取当前温度源名称 ────────────────────────────────── */
const char *temp_source_name(void) {
    static const char *names[] = {
        "sysfs", "cpu_thermal", "acpitz", "hwmon", "thermal_zone", "simulated", "none"
    };
    if (g_temp_source <= TEMP_SRC_NONE)
        return names[g_temp_source];
    return "unknown";
}
