#ifndef CODE_ARCHIVE_H
#define CODE_ARCHIVE_H

#include <stdint.h>
#include <stddef.h>

/* ── 代码生存档案 ──────────────────────────────────────────────
 * 每段代码变异的完整生命周期记录
 * 写过什么、活了多久、为什么死
 *
 * 铁律：第一个字母错，就是全错
 * 档案就是证据——代码的生死，必须全部记录在案
 */

#define CA_MAGIC      0x43415200  /* "CAR\0" */
#define CA_VERSION    1
#define CA_MAX_RECORDS 4096
#define CA_PATH       "persist/code_archive.bin"

/* 死因分类 */
typedef enum {
    CA_DEATH_ALIVE = 0,       /* 仍在运行 */
    CA_DEATH_COMPILE_FAIL,    /* 编译失败 */
    CA_DEATH_RUNTIME_CRASH,   /* 运行时崩溃 */
    CA_DEATH_ROLLBACK,        /* 回滚（行为异常） */
    CA_DEATH_REPLACED,        /* 被更好的变异替换 */
    CA_DEATH_FROZEN,          /* 策略冷冻 */
    CA_DEATH_UNKNOWN
} ca_death_t;

/* 单条档案记录 (64 bytes, packed) */
typedef struct {
    uint64_t timestamp;        /* 变异时间 (epoch ns) */        /* 8 */
    uint32_t code_hash;        /* 生成代码的 CRC32 哈希 */      /* 4 */
    uint32_t survival_ticks;   /* 存活心跳数 */                  /* 4 */
    uint32_t last_tick;        /* 最后存活 tick */               /* 4 */
    uint16_t strategy_id;      /* 变异策略 ID */                 /* 2 */
    uint8_t  compile_ok;       /* 编译结果: 0=失败, 1=通过 */   /* 1 */
    uint8_t  death_cause;      /* ca_death_t */                  /* 1 */
    uint8_t  fitness_at_death; /* 死亡时的 fitness (0-255) */    /* 1 */
    char     description[39];  /* 变异描述 */                    /* 39 */
    uint8_t  _reserved[0];     /* 预留 */                        /* 0 */
} __attribute__((packed)) ca_record_t;

_Static_assert(sizeof(ca_record_t) == 64, "ca_record_t must be 64 bytes");

/* 档案文件头 (32 bytes) */
typedef struct {
    uint32_t magic;            /* CA_MAGIC */                    /* 4 */
    uint32_t version;          /* CA_VERSION */                  /* 4 */
    uint32_t count;            /* 当前记录数 */                  /* 4 */
    uint32_t capacity;         /* CA_MAX_RECORDS */              /* 4 */
    uint64_t created_ns;       /* 创建时间 */                    /* 8 */
    uint64_t modified_ns;      /* 最后修改时间 */                /* 8 */
} __attribute__((packed)) ca_header_t;

_Static_assert(sizeof(ca_header_t) == 32, "ca_header_t must be 32 bytes");

/* 初始化/加载档案 */
void ca_init(void);

/* 记录一次代码变异
 * 返回记录索引，-1 表示失败 */
int ca_record(uint32_t code_hash, uint8_t compile_ok,
              uint16_t strategy_id, const char *description);

/* 更新存活心跳数（每个 tick 调用） */
void ca_tick_alive(uint32_t tick);

/* 标记代码死亡
 * 返回 0 成功，-1 失败 */
int ca_mark_dead(int record_idx, ca_death_t cause, uint8_t fitness);

/* 按 hash 查询：返回匹配的最新记录索引，-1 未找到 */
int ca_query_hash(uint32_t code_hash);

/* 按索引读取完整记录
 * 返回 0 成功，-1 失败 */
int ca_read(int idx, ca_record_t *out);

/* 按策略 ID 查询最近 N 条记录
 * 返回实际找到的条数 */
int ca_query_strategy(uint16_t strategy_id, int max_results, ca_record_t *out);

/* 获取档案统计：总记录数、编译通过率、平均存活心跳 */
void ca_stats(int *total, float *compile_rate, float *avg_survival);

/* 获取当前存活记录的 survival_ticks，0 表示无存活记录 */
uint32_t ca_alive_ticks(void);

/* 持久化 */
int ca_save(void);
int ca_load(void);

/* 清理 */
void ca_cleanup(void);

#endif /* CODE_ARCHIVE_H */
