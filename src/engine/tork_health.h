/**
 * tork_health.h — TORK 第一课：自保
 * 资源监控 + 自动备份 + 自我诊断
 *
 * TORK 必须知道自己状态好不好，才能决定下一步做什么。
 * 如果快死了，要留遗言；如果状态佳，可以冒险。
 */

#ifndef TORK_HEALTH_H
#define TORK_HEALTH_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 健康状态枚举 ====================

typedef enum {
    HEALTH_CRITICAL = -2,  // 快死了，只够保命
    HEALTH_POOR    = -1,   // 状态差，别干重活
    HEALTH_FAIR    = 0,    // 还行，能跑日常
    HEALTH_GOOD    = 1,    // 状态好，可以搞事
    HEALTH_EXCELLENT = 2,  // 满血，随便浪
} tork_health_level_t;

// ==================== 健康指标 ====================

typedef struct {
    // CPU 指标
    double cpu_load;         // 0.0 - 1.0
    double cpu_temp;         // 摄氏度, 0=未知

    // 内存指标
    double mem_used_mb;      // 已用内存 MB
    double mem_total_mb;     // 总内存 MB
    double mem_ratio;        // 已用比例 0.0 - 1.0

    // 磁盘指标
    double disk_used_mb;     // 已用磁盘 MB
    double disk_free_mb;     // 剩余磁盘 MB (数据目录)

    // 进程指标
    int    uptime_seconds;   // 本次运行时长
    int    pid;              // 进程 ID
    int    fork_count;       // fork/clone 次数
    int    crash_count;      // 崩溃恢复次数

    // 灵魂指标
    int    soul_version;     // 当前灵魂版本号
    int    generation;       // 第几代
    int    self_mod_count;   // 自修改次数
    int    golden_integrity; // 金板完整性 CRC 校验 (0=损坏, 1=完好)

    // 综合
    tork_health_level_t level;
    char   diagnosis[256];   // 诊断结论
} tork_health_t;

// ==================== 自动备份 ====================

typedef struct {
    char   soul_backup[512];      // 金板备份路径
    char   code_backup[512];      // 代码备份路径
    time_t timestamp;
    int    version;
    int    integrity_ok;          // 备份完整性
} tork_backup_t;

// ==================== 公共 API ====================

/**
 * 采集全面健康指标
 * 在 tork_engine 主循环中调用，建议每 100 次心跳采集一次
 */
void tork_health_collect(tork_health_t *h);

/**
 * 快速诊断：基于当前指标给出健康等级
 * 不采集新数据，直接分析已有指标
 */
tork_health_level_t tork_health_diagnose(const tork_health_t *h);

/**
 * 自动备份：保存灵魂金板 + 关键代码到备份目录
 * 返回备份版本号，0 表示失败
 */
int tork_health_backup(tork_backup_t *out);

/**
 * 从最近的健康备份恢复
 * version: 指定版本号，0=最新
 * 返回 1=成功, 0=失败
 */
int tork_health_restore(int version);

/**
 * 检查备份目录中有多少可用备份
 */
int tork_health_backup_count(void);

/**
 * 健康报告：生成人类可读的状态摘要
 * 写入 buf，最大 bufsize 字节
 */
void tork_health_report(const tork_health_t *h, char *buf, int bufsize);

/**
 * 紧急信号：当健康等级 HEALTH_CRITICAL 时调用
 * 保存最后状态到 emergency.bin，然后安全终止
 * 这是 TORK 的"遗言"
 */
void tork_health_emergency_exit(const tork_health_t *h, const char *reason);

#ifdef __cplusplus
}
#endif

#endif /* TORK_HEALTH_H */
