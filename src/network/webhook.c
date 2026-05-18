#include "webhook.h"
#include "tork_http.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void webhook_init(webhook_manager_t *mgr) {
    memset(mgr, 0, sizeof(*mgr));
    mgr->count = 0;
}

int webhook_add(webhook_manager_t *mgr, const char *url, int timeout_ms) {
    if (mgr->count >= WEBHOOK_MAX_TARGETS) return -1;
    webhook_target_t *t = &mgr->targets[mgr->count];
    strncpy(t->url, url, WEBHOOK_MAX_URL - 1);
    t->url[WEBHOOK_MAX_URL - 1] = '\0';
    t->timeout_ms = (timeout_ms > 0) ? timeout_ms : 5000;
    t->active = 1;
    mgr->count++;
    return mgr->count - 1;
}

int webhook_remove(webhook_manager_t *mgr, int index) {
    if (index < 0 || index >= mgr->count) return -1;
    for (int i = index; i < mgr->count - 1; i++)
        mgr->targets[i] = mgr->targets[i + 1];
    mgr->count--;
    return 0;
}

int webhook_send_to(webhook_target_t *target, const char *event_type, const char *json_body) {
    if (!target->active) return -1;

    char full_body[WEBHOOK_MAX_BODY + 128];
    int n = snprintf(full_body, sizeof(full_body),
        "{\"event\":\"%s\",\"time\":%ld,\"data\":%s}",
        event_type ? event_type : "unknown",
        (long)time(NULL),
        json_body ? json_body : "{}");

    if (n >= (int)sizeof(full_body)) return -1;

    tork_http_response_t resp;
    int r = tork_http_post(target->url, full_body, "Content-Type: application/json", target->timeout_ms, &resp);
    if (r == 0 && resp.status_code >= 200 && resp.status_code < 300) {
        return 0;
    }
    return -1;
}

int webhook_send(webhook_manager_t *mgr, const char *event_type, const char *json_body) {
    int sent = 0;
    for (int i = 0; i < mgr->count; i++) {
        if (webhook_send_to(&mgr->targets[i], event_type, json_body) == 0)
            sent++;
    }
    return sent;
}
