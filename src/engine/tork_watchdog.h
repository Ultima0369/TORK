#ifndef TORK_WATCHDOG_H
#define TORK_WATCHDOG_H

/* ══════════════════════════════════════════════════════════════
 * TORK 硬件看门狗抽象层
 *
 * 在汇编核心的软件 TSC 时序防御之外，提供硬件级看门狗支持。
 * 如果 tork_engine 死锁（mutex 死锁、SIGSTOP、OOM kill），
 * 硬件看门狗会在超时后重置系统。
 *
 * 支持三种模式:
 *   WDG_SOFT — 纯软件看门狗 (基于 TSC, 已有)
 *   WDG_GPIO — GPIO 刷新 (树莓派/BeagleBone 外接看门狗 IC)
 *   WDG_I2C  — I2C 看门狗 (PMIC 内置, 如 TPS382x / MAX6369)
 * ══════════════════════════════════════════════════════════════ */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 看门狗模式 ────────────────────────────────────────── */
typedef enum {
    WDG_DISABLED = 0,
    WDG_SOFT     = 1,   /* 纯软件 (TSC 时序 + Soul CRC) */
    WDG_GPIO     = 2,   /* GPIO 脉冲刷新 */
    WDG_I2C      = 3,   /* I2C 写入刷新 */
} tork_watchdog_mode_t;

/* ── GPIO 看门狗配置 (树莓派) ──────────────────────────── */
typedef struct {
    int gpio_pin;           /* BCM pin number, 例如 17 */
    int pulse_width_us;     /* 脉冲宽度 (微秒), 通常 1-10 */
    int timeout_ms;         /* 超时时间 (毫秒) */
} tork_watchdog_gpio_t;

/* ── I2C 看门狗配置 ────────────────────────────────────── */
typedef struct {
    int i2c_bus;            /* /dev/i2c-N 总线号 */
    int i2c_addr;           /* 7-bit 设备地址 */
    int timeout_ms;         /* 超时时间 */
    uint8_t feed_reg;       /* 喂狗寄存器地址 */
    uint8_t feed_val;       /* 喂狗写入值 */
} tork_watchdog_i2c_t;

/* ── 看门狗配置 ────────────────────────────────────────── */
typedef struct {
    tork_watchdog_mode_t mode;
    union {
        tork_watchdog_gpio_t gpio;
        tork_watchdog_i2c_t  i2c;
    } config;
    int enabled;
} tork_watchdog_t;

/* ── 看门狗状态 ────────────────────────────────────────── */
typedef struct {
    tork_watchdog_mode_t mode;
    int                  running;
    uint32_t             last_feed_ms;
    uint32_t             feed_count;
    int                  error;         /* 最后一次错误码 */
    char                 error_msg[64]; /* 错误描述 */
} tork_watchdog_status_t;

/* ── API ────────────────────────────────────────────────── */

/* 初始化看门狗 */
int tork_watchdog_init(tork_watchdog_t *wd);

/* 配置看门狗模式 (启动时调用一次) */
int tork_watchdog_configure(tork_watchdog_t *wd, tork_watchdog_mode_t mode);

/* 喂狗 (每个 tick 调用) */
int tork_watchdog_feed(tork_watchdog_t *wd);

/* 获取状态 */
void tork_watchdog_status(tork_watchdog_t *wd, tork_watchdog_status_t *status);

/* 禁用看门狗 */
void tork_watchdog_disable(tork_watchdog_t *wd);

/* ── 平台特定: GPIO 刷新 (Linux /sys/class/gpio) ──────── */
int tork_watchdog_gpio_feed(tork_watchdog_gpio_t *gpio);

/* 平台特定: I2C 刷新 (Linux /dev/i2c-N) */
int tork_watchdog_i2c_feed(tork_watchdog_i2c_t *i2c);

#ifdef __cplusplus
}
#endif

#endif /* TORK_WATCHDOG_H */
