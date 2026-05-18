#ifndef SLIME_MOLD_H
#define SLIME_MOLD_H

/* ── 黏菌算法引擎 ──────────────────────────────────────────
 *  基于Physarum polycephalum(多头绒泡菌)的生长行为建模。
 *  在空间中寻找最优路径拓扑，自适应网络生长与收缩。
 *  应用: 交通流、管道布局、语义连接、工作流编排。
 * ──────────────────────────────────────────────────────────── */

#include <stdint.h>

/* ── 网格常量 ────────────────────────────────────────────── */
#define SLIME_GRID_W       64    /* 网格宽度 (单元) */
#define SLIME_GRID_H       64    /* 网格高度 (单元) */
#define SLIME_MAX_NODES    32    /* 最大节点数 (兴趣点) */
#define SLIME_MAX_TUBES    256   /* 最大管道数 (连接) */
#define SLIME_NAME_LEN     32

/* ── 节点类型 ────────────────────────────────────────────── */
typedef enum {
    SLIME_NODE_SOURCE = 0,    /* 源: 持续分泌营养 */
    SLIME_NODE_SINK   = 1,    /* 汇: 吸收营养 */
    SLIME_NODE_WAY    = 2,    /* 路径点: 被动传递 */
    SLIME_NODE_BLOCK  = 3     /* 障碍: 不可通过 */
} slime_node_type_t;

/* ── 节点 (兴趣点) ────────────────────────────────────────── */
typedef struct {
    char             name[SLIME_NAME_LEN];
    slime_node_type_t type;
    uint8_t          x, y;           /* 网格坐标 */
    float            nutrient_in;    /* 营养输入速率 */
    float            nutrient_out;   /* 营养消耗速率 */
    uint32_t         pulse_count;    /* 脉冲计数 (循环节拍) */
    float            phase;          /* 脉冲相位 0-2π */
} slime_node_t;

/* ── 管道 (菌丝连接) ────────────────────────────────────── */
typedef struct {
    uint16_t         node_a;         /* 节点A索引 */
    uint16_t         node_b;         /* 节点B索引 */
    float            diameter;       /* 管道直径 (0 = 已收缩) */
    float            flow;           /* 当前流量 */
    float            conductivity;   /* 电导率 (决定营养通过效率) */
    float            length;         /* 欧氏距离 */
    uint64_t         last_flow_tick; /* 上次有流量时间 */
    uint32_t         traffic_count;  /* 总流量计数 (使用频率) */
} slime_tube_t;

/* ── 黏菌网络 ────────────────────────────────────────────── */
typedef struct {
    slime_node_t     nodes[SLIME_MAX_NODES];
    uint16_t         node_count;

    slime_tube_t     tubes[SLIME_MAX_TUBES];
    uint16_t         tube_count;

    /* 网格层: 每个格子存储当前营养浓度 */
    float            grid[SLIME_GRID_H][SLIME_GRID_W];
    float            grid_prev[SLIME_GRID_H][SLIME_GRID_W]; /* 上周期 */

    /* 参数 */
    float            decay_rate;      /* 营养衰减率 (默认 0.1) */
    float            growth_rate;     /* 管道生长率 (默认 0.3) */
    float            shrink_rate;     /* 管道收缩率 (默认 0.1) */
    float            min_diameter;    /* 最小管道直径 (默认 0.01) */
    float            dt;              /* 时间步长 (默认 0.1) */
    uint64_t         tick;
} slime_mold_t;

/* ── 路径查询结果 ────────────────────────────────────────── */
typedef struct {
    uint16_t         path_len;        /* 路径长度 (节点数) */
    uint16_t         nodes[SLIME_MAX_NODES]; /* 经过的节点索引 */
    float            total_cost;      /* 总代价 */
    float            reliability;     /* 可靠性 0-1 */
} slime_path_t;

/* ── API ──────────────────────────────────────────────────── */

/* 初始化黏菌网络 */
void slime_init(slime_mold_t *sm);

/* 添加节点 (返回索引, -1 失败) */
int slime_add_node(slime_mold_t *sm, const char *name,
                   slime_node_type_t type, uint8_t x, uint8_t y);

/* 在两个节点间建立初始管道 (自动计算长度) */
int slime_connect(slime_mold_t *sm, uint16_t node_a, uint16_t node_b);

/* 在节点间建立全连接网格 (所有节点对连接) */
void slime_connect_all(slime_mold_t *sm);

/* 模拟一步: 营养传播 + 管道生长/收缩 */
void slime_step(slime_mold_t *sm);

/* 模拟 N 步直到收敛 */
void slime_converge(slime_mold_t *sm, uint32_t max_steps, float threshold);

/* 查询两点间最优路径 (Dijkstra 在管道网络上) */
int slime_find_path(const slime_mold_t *sm,
                    uint16_t from, uint16_t to,
                    slime_path_t *path);

/* 获取前 K 条备选路径 */
int slime_find_alt_paths(const slime_mold_t *sm,
                         uint16_t from, uint16_t to,
                         slime_path_t *paths, int max_paths);

/* 管道脉冲: 给指定节点一个营养脉冲 (模拟周期性需求) */
void slime_pulse(slime_mold_t *sm, uint16_t node_idx, float amount);

/* 动态调整: 根据外部流量数据调整节点营养输入 */
void slime_set_demand(slime_mold_t *sm, uint16_t node_idx, float demand);

/* 获取节点/管道统计 */
uint16_t slime_node_count(const slime_mold_t *sm);
uint16_t slime_tube_count(const slime_mold_t *sm);
float    slime_total_conductivity(const slime_mold_t *sm);

/* 打印当前网络拓扑 */
void slime_print_network(const slime_mold_t *sm);

/* 导出结果为 DOT 格式 (Graphviz) */
void slime_export_dot(const slime_mold_t *sm, const char *path);

/* ── 工作流编排专用 ────────────────────────────────────── */
/* 把工作流步骤映射为黏菌节点，执行路径自动生长 */

typedef enum {
    SLIME_HOOK_NONE  = 0,
    SLIME_HOOK_INPUT = 1,     /* 钩子: 输入 */
    SLIME_HOOK_OUTPUT = 2,    /* 钩子: 输出 */
    SLIME_HOOK_BIDIR = 3     /* 钩子: 双向 */
} slime_hook_type_t;

/* 工作流节点: 一个可执行的模块 */
typedef struct {
    char              name[SLIME_NAME_LEN];
    slime_hook_type_t hook_type;      /* 连接类型 */
    uint16_t          node_idx;       /* 对应的黏菌节点 */
    float             compute_cost;   /* 计算代价 */
    float             memory_cost;    /* 内存代价 */
    void            (*exec_fn)(void*); /* 执行函数 (可为 NULL) */
} slime_workflow_node_t;

#define SLIME_MAX_WF_NODES 32

/* 工作流编排器 */
typedef struct {
    slime_workflow_node_t wf_nodes[SLIME_MAX_WF_NODES];
    uint16_t              wf_count;
    slime_mold_t         *mold;         /* 底层黏菌网络 */
    uint16_t              active_path[SLIME_MAX_NODES];
    uint16_t              active_path_len;
} slime_workflow_t;

/* 初始化工作流编排器 */
void slime_wf_init(slime_workflow_t *wf, slime_mold_t *mold);

/* 注册工作流节点 */
int slime_wf_register(slime_workflow_t *wf, const char *name,
                      slime_hook_type_t hook, float compute, float mem,
                      void (*exec)(void*));

/* 连接工作流节点 (建立钩子) */
int slime_wf_connect(slime_workflow_t *wf, uint16_t a, uint16_t b);

/* 运行工作流: 黏菌自动选择最优路径执行 */
int slime_wf_run(slime_workflow_t *wf, uint16_t start, uint16_t end);

#endif /* SLIME_MOLD_H */
