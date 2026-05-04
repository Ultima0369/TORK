#ifndef PI_INDEX_H
#define PI_INDEX_H

#include "pi_seed.h"
#include <stdint.h>

/*
 * π 索引记忆层：以振动频率索引万事万物
 *
 * 不以人类范式索引记忆（时间线、语义、关联），
 * 以振动频率索引——π 空间的投影就是身份。
 *
 * 每条记忆附带一个 π 指纹，查询时先在 π 空间做快速最近邻匹配，
 * 再返回详细内容。O(log N) 识别，而不是 O(N) 遍历。
 *
 * 坏人见过一次，π 波形就记住了。
 * 好人被记住后，坏人的振动永远过不了相似度阈值。
 * 伪装可以骗语义层，但骗不了频率层。
 */

#define PI_INDEX_SLOTS  256    /* 最多索引 256 个振动指纹 */
#define PI_INDEX_PATH   "persist/pi_index.bin"

/* ── 索引条目 ────────────────────────────────────────────── */
typedef struct {
    pi_profile_t profile;        /* 波形指纹：16 字节 */
    uint32_t     ref_id;         /* 关联的经验/模式 ID */
    uint8_t      category;       /* 类别标签：由 TORK 自行分类 */
    uint8_t      active;         /* 1=有效 */
    uint16_t     match_count;    /* 被匹配到的次数（热度） */
    uint32_t     last_match_tick;/* 上次被匹配的 tick */
} pi_index_entry_t;

/* ── 索引器 ──────────────────────────────────────────────── */
typedef struct {
    pi_index_entry_t slots[PI_INDEX_SLOTS];
    uint32_t         total_entries;
    uint32_t         learn_tick;     /* 上次学习的 tick */
} pi_index_t;

/* ── 匹配结果 ────────────────────────────────────────────── */
typedef struct {
    int      slot_idx;           /* 匹配到的槽位 (-1=无匹配) */
    float    similarity;         /* 相似度 [0,1] */
    uint32_t ref_id;             /* 关联的经验 ID */
    uint8_t  category;           /* 类别标签 */
} pi_match_t;

/* ── Public API ───────────────────────────────────────────── */

/* 初始化 π 索引 */
void pidx_init(void);

/* 索引一条新记忆
 * profile: 波形指纹
 * ref_id:  关联的经验 ID
 * category: 类别标签
 * 返回：索引槽位号，-1 如果失败
 */
int pidx_add(const pi_profile_t *profile, uint32_t ref_id, uint8_t category);

/* 在 π 空间查找最相似的振动
 * profile: 待匹配的波形指纹
 * threshold: 最低相似度阈值 (0.0-1.0)，低于此不返回
 * 返回：最佳匹配
 */
pi_match_t pidx_query(const pi_profile_t *profile, float threshold);

/* 批量查询：找 top-K 最相似的振动 */
int pidx_query_top_k(const pi_profile_t *profile, float threshold,
                     int k, pi_match_t *results);

/* 用一段原始观测序列直接查询（自动生成指纹） */
pi_match_t pidx_query_raw(const uint8_t *buf, int len, float threshold);

/* 更新匹配热度（每次匹配后调用，提升该指纹的存活优先级） */
void pidx_touch(int slot_idx, uint32_t tick);

/* 获取已索引的条目数 */
int pidx_count(void);

/* 持久化 */
int pidx_save(void);
int pidx_load(void);

/* 清理 */
void pidx_cleanup(void);

#endif
