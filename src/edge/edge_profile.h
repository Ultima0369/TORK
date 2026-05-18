#ifndef EDGE_PROFILE_H
#define EDGE_PROFILE_H

/* ══════════════════════════════════════════════════════════════
 * TORK 边缘部署配置
 *
 * 针对 ARM / RISC-V 等低功耗平台的编译时优化。
 * 通过减小内存占用、禁用非必要模块、调整 tick 频率，
 * 让 TORK 能在树莓派 Zero / ESP32 / STM32 上运行。
 * ══════════════════════════════════════════════════════════════ */

/* ── 是否是边缘编译 ────────────────────────────────────── */
#ifdef TORK_EDGE
  /* 禁用非必要的学习模块以节省内存 */
  #ifndef TORK_DISABLE_MCTS
  #define TORK_DISABLE_MCTS          1
  #endif
  #ifndef TORK_DISABLE_DISTRIBUTED
  #define TORK_DISABLE_DISTRIBUTED   1
  #endif
  #ifndef TORK_DISABLE_BRIDGE
  #define TORK_DISABLE_BRIDGE        1
  #endif
  #ifndef TORK_DISABLE_ROLLBACK
  #define TORK_DISABLE_ROLLBACK      1    /* 边缘设备无自修改代码 */
  #endif

  /* 减小时空占用 */
  #ifndef TORK_MCTS_MAX_NODES
  #define TORK_MCTS_MAX_NODES        64   /* 从 512 缩小 */
  #endif
  #ifndef TORK_EXP_BUFFER_SIZE
  #define TORK_EXP_BUFFER_SIZE       200  /* 从 10000 缩小 */
  #endif
  #ifndef TORK_SOUL_SIZE
  #define TORK_SOUL_SIZE             64   /* 从 208 缩小 */
  #endif
  #ifndef TORK_DIST_MAX_PEERS
  #define TORK_DIST_MAX_PEERS        4
  #endif

  /* 更快的 tick */
  #ifndef TORK_IDLE_MS
  #define TORK_IDLE_MS               200  /* 200ms (省电) */
  #endif
  #ifndef TORK_ACTIVE_MS
  #define TORK_ACTIVE_MS             50   /* 50ms */
  #endif

  /* 传感器专用 */
  #ifndef TORK_EDGE_SENSOR_POLL
  #define TORK_EDGE_SENSOR_POLL      1000 /* 1s 轮询一次 */
  #endif

  /* 预警配置 */
  #ifndef TORK_EDGE_ALERT_PERSIST
  #define TORK_EDGE_ALERT_PERSIST    1    /* 预警持久化到文件 */
  #endif
#endif /* TORK_EDGE */

#endif /* EDGE_PROFILE_H */
