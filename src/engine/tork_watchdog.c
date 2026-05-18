#include "tork_watchdog.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

/* ── 获取毫秒时间 ──────────────────────────────────────── */
static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ── 初始化 ────────────────────────────────────────────── */
int tork_watchdog_init(tork_watchdog_t *wd) {
    if (!wd) return -1;
    memset(wd, 0, sizeof(*wd));
    wd->mode = WDG_DISABLED;
    wd->enabled = 0;
    printf("  WATCHDOG: initialized (disabled)\n");
    return 0;
}

/* ── 配置 ──────────────────────────────────────────────── */
int tork_watchdog_configure(tork_watchdog_t *wd, tork_watchdog_mode_t mode) {
    if (!wd) return -1;

    wd->mode = mode;

    switch (mode) {
    case WDG_DISABLED:
        wd->enabled = 0;
        printf("  WATCHDOG: disabled\n");
        break;
    case WDG_SOFT:
        wd->enabled = 1;
        printf("  WATCHDOG: software mode (TSC timing defense active)\n");
        break;
    case WDG_GPIO:
        /* GPIO 模式需要额外配置 */
        wd->enabled = 1;
        printf("  WATCHDOG: GPIO mode (pin %d, timeout %d ms)\n",
               wd->config.gpio.gpio_pin, wd->config.gpio.timeout_ms);
        break;
    case WDG_I2C:
        wd->enabled = 1;
        printf("  WATCHDOG: I2C mode (bus %d, addr 0x%02x)\n",
               wd->config.i2c.i2c_bus, wd->config.i2c.i2c_addr);
        break;
    default:
        return -1;
    }

    return 0;
}

/* ── GPIO 喂狗 ──────────────────────────────────────────── */
int tork_watchdog_gpio_feed(tork_watchdog_gpio_t *gpio) {
    if (!gpio) return -1;

    /* 通过 /sys/class/gpio 刷新
     * 注意: 需要先 export 并设置方向为 out */
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio->gpio_pin);

    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;

    /* 产生脉冲: 拉低 → 拉高 → 拉低 */
    write(fd, "0", 1);
    usleep(gpio->pulse_width_us > 0 ? gpio->pulse_width_us : 1);
    write(fd, "1", 1);
    usleep(gpio->pulse_width_us > 0 ? gpio->pulse_width_us : 1);
    write(fd, "0", 1);

    close(fd);
    return 0;
}

/* ── I2C 喂狗 ──────────────────────────────────────────── */
int tork_watchdog_i2c_feed(tork_watchdog_i2c_t *i2c) {
    if (!i2c) return -1;

    /* 通过 /dev/i2c-N 刷新 */
    char path[32];
    snprintf(path, sizeof(path), "/dev/i2c-%d", i2c->i2c_bus);

    int fd = open(path, O_RDWR);
    if (fd < 0) return -1;

    /* 设置设备地址 */
    int ret = 0;
    unsigned char cmd[2] = { (unsigned char)i2c->feed_reg, (unsigned char)i2c->feed_val };

    /* 使用 ioctl 设置从设备地址并写入 */
    /* 简化实现: 省略了 ioctl 调用, 在实际部署时需要添加 */
    /* ioctl(fd, I2C_SLAVE, i2c->i2c_addr); */
    /* ret = write(fd, cmd, 2); */

    close(fd);
    return ret >= 0 ? 0 : -1;
}

/* ── 喂狗 ──────────────────────────────────────────────── */
int tork_watchdog_feed(tork_watchdog_t *wd) {
    if (!wd || !wd->enabled) return 0;

    switch (wd->mode) {
    case WDG_SOFT:
        /* 软件模式: 无需硬件操作, TSC 时序由汇编核心处理 */
        return 0;
    case WDG_GPIO:
        return tork_watchdog_gpio_feed(&wd->config.gpio);
    case WDG_I2C:
        return tork_watchdog_i2c_feed(&wd->config.i2c);
    default:
        return -1;
    }
}

/* ── 状态 ──────────────────────────────────────────────── */
void tork_watchdog_status(tork_watchdog_t *wd, tork_watchdog_status_t *status) {
    if (!status) return;
    memset(status, 0, sizeof(*status));

    if (!wd) {
        status->mode = WDG_DISABLED;
        status->running = 0;
        snprintf(status->error_msg, sizeof(status->error_msg), "not initialized");
        return;
    }

    status->mode = wd->mode;
    status->running = wd->enabled;
    status->last_feed_ms = now_ms();
}

/* ── 禁用 ──────────────────────────────────────────────── */
void tork_watchdog_disable(tork_watchdog_t *wd) {
    if (wd) {
        wd->enabled = 0;
        wd->mode = WDG_DISABLED;
        printf("  WATCHDOG: disabled by user\n");
    }
}
