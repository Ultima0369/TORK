#ifndef GROWTH_NODE_H
#define GROWTH_NODE_H

#include <stdint.h>

/* ── 生长节点: 替代Go/No-Go门控 ──────────────────────────────
 *  不是"通过/不通过"的二值判断, 而是"此刻长出了什么"的记录。
 *  活着就成长, 成长就演化——不需要验证"是否有意义"。
 *
 *  记录系统运行中的关键"第一次"事件:
 *    - GMM基线首次收敛
 *    - 恐惧动力学首次响应异常
 *    - 好奇心首次驱动探索
 *    - 代码首次自修改成功
 *    - 快照首次回滚
 *    - 棱镜协议首次建立信任
 *    - 自校准首次与天文观测对齐
 * ──────────────────────────────────────────────────────────── */

#define GROWTH_MAX_NODES  32
#define GROWTH_NAME_LEN   32

typedef enum {
    GROWTH_GMM_CONVERGE       = 0,   /* GMM基线首次收敛 */
    GROWTH_FEAR_RESPONSE      = 1,   /* 恐惧首次响应异常 */
    GROWTH_CURIOSITY_EXPLORE  = 2,   /* 好奇心首次驱动探索 */
    GROWTH_CODE_MODIFY        = 3,   /* 代码首次自修改成功 */
    GROWTH_SNAPSHOT_ROLLBACK  = 4,   /* 快照首次回滚 */
    GROWTH_TRUST_ESTABLISHED  = 5,   /* 棱镜协议首次建立信任 */
    GROWTH_SELF_CALIBRATE     = 6,   /* 自校准首次对齐 */
    GROWTH_HUMILITY_FIRST     = 7,   /* 谦逊机制首次生效 */
    GROWTH_STRUCT_CHECK       = 8,   /* 结构约束首次阻止行动 */
    GROWTH_KNOWLEDGE_STOP     = 9,   /* 知识停止: 信息超出验证能力 */
    GROWTH_NUM_TYPES          = 10
} growth_type_t;

typedef struct {
    growth_type_t type;
    uint64_t      tick;            /* 发生时的tick */
    float         value;           /* 伴随值(如恐惧强度, drive值等) */
    char          name[GROWTH_NAME_LEN];
} growth_node_t;

typedef struct {
    growth_node_t nodes[GROWTH_MAX_NODES];
    uint32_t      count;
    uint32_t      types_seen;      /* bitmask: 哪些type已经出现过 */
    uint64_t      first_tick;      /* 第一个生长节点的tick */
    uint64_t      last_tick;       /* 最近一个生长节点的tick */
} growth_log_t;

/* 初始化 */
void growth_init(void);

/* 记录一个生长节点(同一type只记录第一次) */
void growth_record(growth_type_t type, uint64_t tick, float value, const char *name);

/* 查询: 某个type是否已经生长过 */
int growth_has(growth_type_t type);

/* 查询: 获取某个type的生长节点(未生长则返回NULL) */
const growth_node_t *growth_get(growth_type_t type);

/* 查询: 已生长的type数量 */
int growth_count(void);

/* 打印生长日志 */
void growth_print(void);

/* 保存/加载 */
int growth_save(void);
int growth_load(void);

#endif /* GROWTH_NODE_H */
