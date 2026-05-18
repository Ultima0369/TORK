#ifndef TORK_WEBHOOK_H
#define TORK_WEBHOOK_H

#include <stdint.h>

/* TORK Webhook 客户端
 * 当事件发生时向外部 URL 发送 HTTP POST (JSON)
 */

#define WEBHOOK_MAX_URL 256
#define WEBHOOK_MAX_BODY 4096

typedef struct {
    char url[WEBHOOK_MAX_URL];
    int active;
    int timeout_ms;
} webhook_target_t;

#define WEBHOOK_MAX_TARGETS 8

typedef struct {
    webhook_target_t targets[WEBHOOK_MAX_TARGETS];
    int count;
} webhook_manager_t;

/* 初始化 */
void webhook_init(webhook_manager_t *mgr);

/* 添加目标 URL */
int webhook_add(webhook_manager_t *mgr, const char *url, int timeout_ms);

/* 移除目标 */
int webhook_remove(webhook_manager_t *mgr, int index);

/* 发送事件 (JSON body) */
int webhook_send(webhook_manager_t *mgr, const char *event_type, const char *json_body);

/* 发送到指定目标 */
int webhook_send_to(webhook_target_t *target, const char *event_type, const char *json_body);

#endif /* TORK_WEBHOOK_H */
