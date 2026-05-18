#ifndef ROLLBACK_H
#define ROLLBACK_H

#include <stdint.h>
#include <stddef.h>

/* ── 常量 ──────────────────────────────────────────────── */

#define RB_MAX_VERSIONS     5       /* 每个文件保留的最新版本数 */
#define RB_MAX_MODULES      16      /* 最大可追踪模块数 */
#define RB_MODULE_NAME_MAX  32      /* 模块名最大长度 */
#define RB_PATH_MAX         256     /* 路径缓冲大小 */
#define RB_BACKUP_DIR       "persist/rollback"  /* 备份目录 */

/* ── 错误码 ────────────────────────────────────────────── */
#define RB_OK               0
#define RB_ERR_INIT        -1      /* 未初始化 */
#define RB_ERR_NOENT       -2      /* 备份不存在 */
#define RB_ERR_CRC         -3      /* CRC 校验失败 */
#define RB_ERR_CORRUPT     -4      /* 备份数据损坏 */
#define RB_ERR_NOMEM       -5      /* 内存不足 */
#define RB_ERR_IO          -6      /* IO 错误 */
#define RB_ERR_TOOMANY     -7      /* 超出最大版本数 */
#define RB_ERR_INVARG      -8      /* 无效参数 */
#define RB_ERR_COMPILE     -9      /* 编译测试失败 */
#define RB_ERR_SANITY      -10     /* 合理性检查失败 */

/* ── 备份记录 ──────────────────────────────────────────── */
typedef struct {
    uint64_t timestamp_ns;      /* 备份时间戳 */
    uint32_t crc32;             /* 备份数据 CRC32 */
    uint16_t data_len;          /* 备份数据长度 */
    uint16_t compile_pass:1;    /* 编译测试是否通过 */
    uint16_t reserved:15;
    char file_path[RB_PATH_MAX]; /* 被备份的原始文件路径 */
} rb_record_t;

/* ── 模块状态 ──────────────────────────────────────────── */
typedef struct {
    char name[RB_MODULE_NAME_MAX];
    int record_count;
    rb_record_t records[RB_MAX_VERSIONS];
    int auto_recover_attempts;   /* 已尝试的自动恢复次数 */
} rb_module_t;

/* ── 回滚系统状态 ──────────────────────────────────────── */
typedef struct {
    rb_module_t modules[RB_MAX_MODULES];
    int module_count;
    int initialized;
    int total_backups;
    int total_restores;
    int total_failures;
} rb_system_t;

/* ── API ───────────────────────────────────────────────── */

/* 初始化回滚系统 */
int rb_init(void);

/* 保存备份: 在修改文件前调用 */
int rb_snapshot(const char *module_name, const char *file_path,
                const void *data, size_t len);

/* 回滚到指定版本 (0=最新, 1=次新, ...) */
int rb_restore(const char *module_name, int version,
               void *buf, size_t *len);

/* 自动恢复: 从最新版本依次尝试，直到编译测试通过 */
int rb_auto_recover(const char *module_name, const char *file_path,
                    void *buf, size_t *len);

/* 编译测试: 验证修改后的代码能否编译 */
int rb_compile_test(const char *file_path, const void *data, size_t len);

/* 合理性检查: 验证修改后的代码是否合理（文件大小、指令完整性等）*/
int rb_sanity_check(const void *data, size_t len);

/* 清理: 删除超出 RB_MAX_VERSIONS 的旧备份 */
int rb_prune(const char *module_name);

/* 列出可用备份版本 */
int rb_list(const char *module_name, rb_record_t *records, int max_records);

/* 获取系统统计 */
void rb_stats(int *total_backups, int *total_restores, int *total_failures);

/* 完整性检查：验证所有备份的CRC */
int rb_verify_integrity(void);

#endif /* ROLLBACK_H */
