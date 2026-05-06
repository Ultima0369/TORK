#ifndef SWARM_H
#define SWARM_H

/* ── 群合并视图 ─────────────────────────────────────────────
 * 查询 beacon + distributed 两个系统的同类感知。
 *
 * 注意：两个系统的 peer ID 格式不同（beacon 用 16 字节 node_id，
 * distributed 用 32 位 instance_id），无法做精确去重。
 * 下面分别返回各自计数，由调用者根据场景选择使用。
 * ──────────────────────────────────────────────────────── */

/* 返回 beacon 系统已知的同类数量（仅统计活跃 peer） */
int swarm_beacon_count(void);

/* 返回 distributed 系统已知的同类数量 */
int swarm_dist_count(void);

/* 初始化（仅标记 beacon_init 依赖就绪） */
int swarm_init(void);

#endif /* SWARM_H */
