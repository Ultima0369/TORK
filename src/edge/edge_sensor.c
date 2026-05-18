#include "edge_sensor.h"
#include <stdio.h>
#include <string.h>

/* ── 传感器注册表 ──────────────────────────────────────── */
#define MAX_SENSORS 32
static struct {
    edge_sensor_cfg_t cfg;
    uint16_t          last_value;
    uint8_t           fault_count;
    uint8_t           present;   /* 1=已注册 */
} g_sensors[MAX_SENSORS];
static int g_num_sensors = 0;
static uint32_t g_total_reads = 0;
static uint32_t g_total_faults = 0;
static int g_bus_initialized = 0;

/* ── 初始化 ────────────────────────────────────────────── */
int edge_sensor_init(void) {
    memset(g_sensors, 0, sizeof(g_sensors));
    g_num_sensors = 0;
    g_total_reads = 0;
    g_total_faults = 0;
    g_bus_initialized = 1;
    printf("  EDGE SENSOR: bus initialized\n");
    return 0;
}

/* ── 注册 ──────────────────────────────────────────────── */
void edge_sensor_register(const edge_sensor_cfg_t *cfg) {
    if (!cfg || g_num_sensors >= MAX_SENSORS) return;
    g_sensors[g_num_sensors].cfg = *cfg;
    g_sensors[g_num_sensors].last_value = cfg->normal_min + (cfg->normal_max - cfg->normal_min) / 2;
    g_sensors[g_num_sensors].fault_count = 0;
    g_sensors[g_num_sensors].present = 1;
    g_num_sensors++;
    printf("  EDGE SENSOR: registered type=%d addr=0x%02X\n",
           cfg->type, cfg->i2c_addr);
}

/* ── 读取 ──────────────────────────────────────────────── */
int edge_sensor_read(edge_sensor_type_t type, edge_sample_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->type = type;
    out->precision = 1;
    out->fault = 1;  /* 默认故障 */

    for (int i = 0; i < g_num_sensors; i++) {
        if (g_sensors[i].cfg.type != type) continue;
        if (!g_sensors[i].present) continue;

        out->value = g_sensors[i].last_value;
        out->fault = (g_sensors[i].fault_count > 5) ? 1 : 0;
        g_total_reads++;
        return 0;
    }

    return -1;  /* 未注册 */
}

/* ── 编码模式向量 ──────────────────────────────────────── */
void edge_sensor_encode_pattern(edge_pattern_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->timestamp_ms = 0;  /* 调用方填入真实时间 */
    out->confidence = 80;
    out->anomaly_score = 0;

    int active = 0;
    for (int i = 0; i < g_num_sensors && active < 16; i++) {
        if (!g_sensors[i].present) continue;
        edge_sensor_cfg_t *cfg = &g_sensors[i].cfg;
        uint16_t v = g_sensors[i].last_value;

        /* 三值编码: 与正常范围比较 */
        if (v < cfg->normal_min) {
            out->pattern[active] = -1;  /* 偏低 */
            /* 偏离越大, 异常分数越高 */
            int dev = (cfg->normal_min - v) * 100 / (cfg->normal_min ? cfg->normal_min : 1);
            if (dev > out->anomaly_score) out->anomaly_score = dev;
        } else if (v > cfg->normal_max) {
            out->pattern[active] = 1;   /* 偏高 */
            int dev = (v - cfg->normal_max) * 100 / cfg->normal_max;
            if (dev > out->anomaly_score) out->anomaly_score = dev;
        } else {
            out->pattern[active] = 0;   /* 正常 */
        }

        active++;
    }

    out->num_active = active;

    /* 置信度: 活跃传感器越多越可靠 */
    if (active >= 8) out->confidence = 95;
    else if (active >= 4) out->confidence = 85;
    else if (active >= 2) out->confidence = 70;
    else out->confidence = 50;
}

/* ── 模拟值 (用于测试/调试) ────────────────────────────── */
void edge_sensor_set_mock(edge_sensor_type_t type, uint16_t value) {
    for (int i = 0; i < g_num_sensors; i++) {
        if (g_sensors[i].cfg.type == type) {
            g_sensors[i].last_value = value;
            g_sensors[i].fault_count = 0;
            return;
        }
    }
}

/* ── 统计 ──────────────────────────────────────────────── */
void edge_sensor_stats(uint32_t *total_reads, uint32_t *faults) {
    if (total_reads) *total_reads = g_total_reads;
    if (faults) *faults = g_total_faults;
}
