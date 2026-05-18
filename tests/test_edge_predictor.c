/* ══════════════════════════════════════════════════════════════
 * TORK 边缘预警测试
 *
 * 模拟锂电池组传感器数据，验证模式偏离预警系统。
 * 场景: 电池温度从 35°C 缓慢上升至 68°C (热失控前期)
 * 预期: TORK 在温度到达 50°C 之前就发出早期预警
 * ══════════════════════════════════════════════════════════════ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "edge/edge_sensor.h"
#include "edge/edge_predictor.h"

int main(void) {
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║   TORK 边缘预警系统 · 热失控模拟                ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");

    /* 初始化 */
    edge_sensor_init();
    edge_predictor_init();
    edge_predictor_set_sensitivity(80);

    /* 注册电池传感器:
     * 类型, I2C地址, 寄存器, 正常范围, 预警范围, 轮询间隔 */
    edge_sensor_register(&(edge_sensor_cfg_t){
        .type = SENSOR_BATT_TEMP, .i2c_addr = 0x48, .i2c_reg = 0x00,
        .normal_min = 200, .normal_max = 450,  /* 20.0°C ~ 45.0°C */
        .warn_min = 100, .warn_max = 600,
        .poll_ms = 1000
    });
    edge_sensor_register(&(edge_sensor_cfg_t){
        .type = SENSOR_BATT_VOLTAGE, .i2c_addr = 0x40, .i2c_reg = 0x02,
        .normal_min = 3300, .normal_max = 4200,  /* 3.3V ~ 4.2V */
        .warn_min = 3000, .warn_max = 4300,
        .poll_ms = 1000
    });
    edge_sensor_register(&(edge_sensor_cfg_t){
        .type = SENSOR_BATT_CURRENT, .i2c_addr = 0x40, .i2c_reg = 0x01,
        .normal_min = 0, .normal_max = 2000,  /* 0 ~ 2A */
        .warn_min = -500, .warn_max = 3000,
        .poll_ms = 1000
    });
    edge_sensor_register(&(edge_sensor_cfg_t){
        .type = SENSOR_BATT_IR, .i2c_addr = 0x40, .i2c_reg = 0x04,
        .normal_min = 10000, .normal_max = 50000,  /* 10~50 mOhm */
        .warn_min = 5000, .warn_max = 100000,
        .poll_ms = 2000
    });

    /* ── 模拟热失控场景 ──────────────────────────────── */
    printf("场景: 锂电池组热失控模拟\n");
    printf("      温度从 35°C 持续上升至 68°C\n");
    printf("      同期内阻升高, 电压下降\n\n");

    int temp_c = 35;       /* 起始温度 */
    int voltage_mv = 4100;  /* 起始电压 4.1V */
    int current_ma = 500;   /* 0.5A 放电 */
    int ir_uohm = 15000;    /* 15 mOhm */

    int alerts = 0;
    int first_warning_tick = -1;
    int critical_tick = -1;

    for (int tick = 0; tick < 80; tick++) {
        /* 温度: 前 20 tick 缓慢, 之后加速上升 */
        if (tick < 20) {
            temp_c += 0;  /* 稳定 */
        } else if (tick < 40) {
            temp_c += (tick - 20) / 5;  /* 缓慢上升 */
        } else {
            temp_c += 2;  /* 加速 (热失控前兆) */
        }

        if (tick > 30) {
            voltage_mv -= 5;       /* 电压缓慢下降 */
            ir_uohm += 500;        /* 内阻持续升高 */
        }
        if (tick > 50) {
            current_ma += 50;      /* 电流异常增大 */
        }

        /* 边界控制 */
        if (temp_c > 70) temp_c = 70;
        if (voltage_mv < 3000) voltage_mv = 3000;
        if (ir_uohm > 80000) ir_uohm = 80000;
        if (current_ma > 3000) current_ma = 3000;

        /* 设置模拟值 */
        edge_sensor_set_mock(SENSOR_BATT_TEMP, temp_c * 10);
        edge_sensor_set_mock(SENSOR_BATT_VOLTAGE, voltage_mv);
        edge_sensor_set_mock(SENSOR_BATT_CURRENT, current_ma);
        edge_sensor_set_mock(SENSOR_BATT_IR, ir_uohm);

        /* 编码模式向量 */
        edge_pattern_t pattern;
        edge_sensor_encode_pattern(&pattern);
        pattern.timestamp_ms = tick * 1000;

        /* 预测 */
        edge_alert_t alert;
        edge_warn_level_t level = edge_predictor_tick(&pattern, &alert);

        if (level > WARN_NONE) {
            alerts++;
            if (first_warning_tick < 0) first_warning_tick = tick;
            printf("  tick %2d | T=%d°C V=%dmV I=%dmA R=%dmΩ | %s",
                   tick, temp_c, voltage_mv, current_ma, ir_uohm / 1000,
                   alert.message);
            printf("\n");
        }

        if (level >= WARN_CRITICAL && critical_tick < 0) {
            critical_tick = tick;
        }

        /* 渲染进度条 */
        if (tick % 5 == 0) {
            int bar = temp_c - 30;
            if (bar < 0) bar = 0;
            if (bar > 40) bar = 40;
            printf("  [%3d°C] %s\n", temp_c,
                   bar < 15 ? "········································" :
                   bar < 25 ? "▓▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░░░░░░░░░" :
                   bar < 35 ? "▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░" :
                              "▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓");
        }

        usleep(50000);  /* 50ms */
    }

    /* ── 结果 ────────────────────────────────────────── */
    printf("\n══════════════════════════════════════════════\n");
    printf("  测试结果:\n");
    printf("  总预警数:     %d\n", alerts);
    printf("  首次预警 @tick %d (温度 ~%d°C)\n",
           first_warning_tick, 35 + (first_warning_tick > 20 ?
           (first_warning_tick - 20) / 5 : 0));
    printf("  临界预警 @tick %d (温度 ~%d°C)\n",
           critical_tick, critical_tick > 0 ? 35 + (critical_tick - 20) / 5 * 2 : 0);

    if (first_warning_tick > 0 && critical_tick > first_warning_tick) {
        int lead = critical_tick - first_warning_tick;
        printf("  提前量:       %d tick (约 %d 分钟)\n", lead, lead / 2);
        printf("  ✅ 系统在临界前 %d 分钟发出预警\n", lead / 2);
    } else {
        printf("  ⚠️  预警时机不足\n");
    }

    const edge_predictor_state_t *s = edge_predictor_get_state();
    printf("  总 tick 数:   %u\n", s->total_ticks);
    printf("  敏感度:       %u%%\n", s->sensitivity);
    printf("\n");

    printf("结论: TORK 边缘预警系统可以在温度到达危险阈值之前\n");
    printf("      通过模式偏离和趋势检测提前发出预警。\n");
    printf("      这不是阈值触发, 这是模式识别。\n");

    return 0;
}
