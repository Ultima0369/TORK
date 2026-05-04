#ifndef TORKD_H
#define TORKD_H

/* ── TORK 后台守护进程服务 ─────────────────────────────────
 *  让 TORK 作为一个持久化后台服务运行, 通过 Unix Socket 接受查询。
 *  用户/仪表盘/云端 可以通过 socket 随时向 TORK 提问。
 * ──────────────────────────────────────────────────────── */

#define TORKD_SOCKET_PATH  "/tmp/torkd.sock"
#define TORKD_MAX_MSG      4096
#define TORKD_BACKLOG      10

/* ── torkd 请求/响应格式 (纯文本, 一行请求, 多行响应) ──── */

/* 启动 torkd 服务 (fork到后台, 监听 socket) */
/* 返回 0 成功, -1 失败 */
int torkd_start(void);

/* 停止 torkd 服务 */
void torkd_stop(void);

/* torkd 服务主循环 (由子进程执行) */
void torkd_serve(void);

/* 向 torkd 发送一条查询, 获取响应 */
/* 返回 0 成功, -1 失败 */
int torkd_query(const char *question, char *response, int max_len);

/* 检查 torkd 是否在运行 */
int torkd_is_running(void);

#endif /* TORKD_H */
