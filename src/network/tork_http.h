/**
 * tork_http.h — TORK 第二课：上网
 * HTTP 客户端 + 网页抓取 + API 调用
 *
 * TORK 必须能自己连上世界，才能服务世界。
 * 这个模块给它一个简单的 HTTP GET/POST 能力。
 */

#ifndef TORK_HTTP_H
#define TORK_HTTP_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 配置 ====================

#define TORK_HTTP_MAX_URL    1024
#define TORK_HTTP_MAX_HEADER 4096
#define TORK_HTTP_MAX_BODY   65536  // 64KB 最大响应
#define TORK_HTTP_TIMEOUT    15     // 默认超时秒数

// ==================== HTTP 方法 ====================

typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
} tork_http_method_t;

// ==================== HTTP 响应 ====================

typedef struct {
    int    status_code;         // 200, 404, 500 ...
    char   status_text[64];     // "OK", "Not Found" ...
    char   headers[TORK_HTTP_MAX_HEADER];
    char   body[TORK_HTTP_MAX_BODY];
    int    body_len;
    int    success;             // 1=请求完成, 0=失败
    char   error[256];          // 失败原因
    double elapsed_sec;         // 耗时
} tork_http_response_t;

// ==================== HTTP 请求 ====================

typedef struct {
    tork_http_method_t method;
    char   url[TORK_HTTP_MAX_URL];
    char   post_data[TORK_HTTP_MAX_BODY / 2];
    int    timeout_sec;
    char   custom_header[TORK_HTTP_MAX_HEADER]; // 额外请求头
} tork_http_request_t;

// ==================== 公共 API ====================

/**
 * 发起 HTTP 请求（同步阻塞）
 * 在 TORK 的主循环中调用，注意不要阻塞太久
 * 建议超时 <= 15 秒
 */
void tork_http_request(const tork_http_request_t *req, tork_http_response_t *resp);

/**
 * 快速 GET 请求
 * 一行调用：url → 响应体字符串
 * 返回 body 指针，失败返回 NULL（结果写入 resp）
 */
const char* tork_http_get(const char *url, tork_http_response_t *resp);

/**
 * 快速 POST 请求（JSON 数据）
 * url: 目标地址
 * json_data: JSON 字符串
 * 返回 body 指针，失败返回 NULL
 */
const char* tork_http_post_json(const char *url, const char *json_data,
                                tork_http_response_t *resp);

/**
 * 获取公网 IP（用于 TORK 的网络身份识别）
 * 从多个 IP 查询服务获取
 * 返回 IP 字符串，写入 buf，失败返回 "unknown"
 */
const char* tork_http_public_ip(char *buf, int bufsize);

/**
 * 简单的网页标题抓取
 * 从 HTML 中提取 <title> 标签内容
 * 最多提取 256 字符
 */
void tork_http_fetch_title(const char *html, char *title, int titlesize);

/**
 * API / 健康检查端点
 * 向某个地址发送 GET，检查是否返回 200
 * 返回 1=在线, 0=离线
 */
int tork_http_ping(const char *url, int timeout_sec);

#ifdef __cplusplus
}
#endif

#endif /* TORK_HTTP_H */
