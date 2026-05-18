#include "../config.h"
#ifndef MCTS_PERSIST_H
#define MCTS_PERSIST_H

#include "mcts.h"

/* ── MCTS 树持久化 ─────────────────────────────────────────
 *  保存/加载 MCTS 搜索树，让学习进度跨进程重启存活。
 *  文件格式: 二进制，首字节版本号 + 节点序列化。
 * ───────────────────────────────────────────────────────── */

#define MCTS_PERSIST_PATH  TORK_MCTS_PATH
#define MCTS_PERSIST_VERSION 1

/* 保存完整搜索树到磁盘 */
int mcts_persist_save(const char *path);

/* 从磁盘加载搜索树 */
int mcts_persist_load(const char *path);

/* 清空树（重置学习） */
void mcts_persist_reset(void);

#endif /* MCTS_PERSIST_H */
