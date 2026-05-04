#ifndef TORKD_H
#define TORKD_H

/* ── TORK 集成 Socket 服务 ─────────────────────────────────
 *  直接嵌入 tork_engine 主循环, 每 tick 非阻塞处理客户端。
 *  外部程序通过 Unix Socket 随时向 TORK 提问。
 * ──────────────────────────────────────────────────────── */

#include <stdint.h>

#define TORKD_SOCKET_PATH  "/tmp/torkd.sock"
#define TORKD_MAX_MSG      4096
#define TORKD_BACKLOG      10

/* 在 engine 启动时调用: 创建 socket 并开始监听 */
int torkd_init(void *soul);

/* 每 tick 在 engine 主循环中调用: 非阻塞处理待决连接 */
void torkd_tick(void);

/* engine 退出时调用: 关闭 socket */
void torkd_shutdown(void);

/* 外部进程查询(连接已有 socket) */
int torkd_query(const char *question, char *response, int max_len);

/* 检查 socket 是否在监听 */
int torkd_is_running(void);

#endif /* TORKD_H */
