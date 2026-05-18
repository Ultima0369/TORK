#ifndef EDGE_SENSOR_H
#define EDGE_SENSOR_H

/* ══════════════════════════════════════════════════════════════
 * TORK 边缘传感器抽象层
 *
 * 统一接口：锂电池组 · 电动汽车 · 工业设备 · 环境监测
 * 适配 I2C / GPIO / ADC / CAN 总线
 *
 * 设计原则：每个传感器返回 "模式向量" 而非原始值。
 * 让 TORK 的 TLN 做模式匹配，而不是人工设阈值。
 * ══════════════════════════════════════════════════════════════ */

#include <stdint.h>
#include "../config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 传感器类型 ────────────────────────────────────────── */
typedef enum {
    SENSOR_NONE          = 0,

    /* 电池组 */
    SENSOR_BATT_VOLTAGE  = 1,   /* 单串电压 (mV) */
    SENSOR_BATT_CURRENT  = 2,   /* 充放电电流 (mA) */
    SENSOR_BATT_TEMP     = 3,   /* 电芯温度 (0.1°C) */
    SENSOR_BATT_IR       = 4,   /* 内阻 (uOhm) */
    SENSOR_BATT_SOC      = 5,   /* 剩余电量 (0-1000, 0.1%) */

    /* 电动汽车 */
    SENSOR_MOTOR_TEMP    = 10,  /* 电机温度 (0.1°C) */
    SENSOR_MOTOR_RPM     = 11,  /* 电机转速 */
    SENSOR_INVERTER_TEMP = 12,  /* 逆变器温度 (0.1°C) */
    SENSOR_BRAKE_TEMP    = 13,  /* 刹车温度 (0.1°C) */

    /* 环境 */
    SENSOR_AMBIENT_TEMP  = 20,  /* 环境温度 (0.1°C) */
    SENSOR_HUMIDITY      = 21,  /* 湿度 (0-1000, 0.1%) */
    SENSOR_VIBRATION     = 22,  /* 振动 (0-1000) */
    SENSOR_GAS_PRESSURE  = 23,  /* 气压 (Pa) */

    /* 系统 */
    SENSOR_CPU_TEMP      = 30,  /* 芯片温度 (0.1°C) */
    SENSOR_CPU_LOAD      = 31,  /* CPU 负载 (0-1000) */
    SENSOR_MEM_USED      = 32,  /* 内存使用率 (0-1000) */
} edge_sensor_type_t;

/* ── 单次采样 ──────────────────────────────────────────── */
typedef struct {
    edge_sensor_type_t type;     /* 传感器类型 */
    uint16_t           value;    /* 原始值 (单位见上) */
    uint8_t            precision;/* 精度标志 (0=粗, 1=正常, 2=高精) */
    uint8_t            fault;    /* 0=正常, 1=通信故障, 2=超范围 */
} edge_sample_t;

/* ── 模式向量 (TLN 输入) ───────────────────────────────── */
/* 由多个传感器采样编码为 TLN 可直接消费的三值模式 */
typedef struct {
    uint32_t timestamp_ms;       /* 时间戳 */
    uint8_t  num_active;         /* 活跃传感器数量 (1-16) */
    int8_t   pattern[16];        /* 三值模式: -1=偏低, 0=正常, +1=偏高 */
    uint8_t  confidence;         /* 模式置信度 (0-100) */
    uint16_t anomaly_score;      /* 异常分数 (0=正常, 65535=临界) */
} edge_pattern_t;

/* ── 传感器配置 ────────────────────────────────────────── */
typedef struct {
    edge_sensor_type_t type;
    uint8_t            i2c_addr;     /* I2C 地址 (0=GPIO/ADC) */
    uint8_t            i2c_reg;      /* 寄存器地址 */
    uint16_t           normal_min;   /* 正常范围下限 */
    uint16_t           normal_max;   /* 正常范围上限 */
    uint16_t           warn_min;     /* 预警范围下限 */
    uint16_t           warn_max;     /* 预警范围上限 */
    uint16_t           poll_ms;      /* 轮询间隔 (ms) */
} edge_sensor_cfg_t;

/* ── 公共 API ──────────────────────────────────────────── */

/* 初始化传感器总线 (I2C/GPIO/ADC)
 * 返回: 0=ok, -1=总线初始化失败 */
int edge_sensor_init(void);

/* 注册传感器配置 */
void edge_sensor_register(const edge_sensor_cfg_t *cfg);

/* 读取传感器 → 填充采样值
 * 非阻塞: 返回最新缓存值 */
int edge_sensor_read(edge_sensor_type_t type, edge_sample_t *out);

/* 读取所有注册传感器 → 编码为模式向量
 * 这是 TORK 每次 tick 调用的主入口 */
void edge_sensor_encode_pattern(edge_pattern_t *out);

/* 设置传感器模拟值 (用于测试/调试) */
void edge_sensor_set_mock(edge_sensor_type_t type, uint16_t value);

/* 获取累计读数统计 */
void edge_sensor_stats(uint32_t *total_reads, uint32_t *faults);

#ifdef __cplusplus
}
#endif

#endif /* EDGE_SENSOR_H */
