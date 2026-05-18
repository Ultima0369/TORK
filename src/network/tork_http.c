/**
 * tork_http.c — TORK 上网实现
 *
 * Windows: WinHTTP API
 * Linux:   POSIX socket + DNS
 *
 * 纯 C 零外部依赖。
 */

#include "tork_http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#endif

// ==================== 内部工具 ====================

static void url_parse(const char *url, char *host, int hostsize,
                      char *path, int pathsize, int *port) {
    *port = 80;
    host[0] = 0; path[0] = 0;
    const char *p = url;

    // 跳过协议
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        *port = 443;
    }

    // 提取 host
    int i = 0;
    while (*p && *p != '/' && *p != ':' && i < hostsize - 1)
        host[i++] = *p++;
    host[i] = 0;

    // 端口
    if (*p == ':') {
        p++;
        *port = atoi(p);
        while (*p && *p != '/') p++;
    }

    // 路径
    if (!*p) {
        snprintf(path, pathsize, "/");
    } else {
        snprintf(path, pathsize, "%s", p);
    }
}

// ==================== Windows 实现 ====================

#ifdef _WIN32

static int winhttp_request(const tork_http_request_t *req, tork_http_response_t *resp) {
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    int ret = 0;

    char host[256], path[2048];
    int port;
    url_parse(req->url, host, sizeof(host), path, sizeof(path), &port);

    hSession = WinHttpOpen(L"TORK/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           NULL, NULL, 0);
    if (!hSession) goto cleanup;

    // 宽字符转换
    wchar_t whost[256];
    MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, 256);

    hConnect = WinHttpConnect(hSession, whost, port, 0);
    if (!hConnect) goto cleanup;

    const wchar_t *methods[] = { L"GET", L"POST", L"PUT", L"DELETE" };
    wchar_t wpath[2048];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 2048);

    hRequest = WinHttpOpenRequest(hConnect, methods[req->method],
                                  wpath, NULL, NULL, NULL,
                                  port == 443 ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) goto cleanup;

    // 超时
    int timeout = (req->timeout_sec > 0 ? req->timeout_sec : TORK_HTTP_TIMEOUT) * 1000;
    WinHttpSetTimeouts(hRequest, timeout, timeout, timeout, timeout);

    // 自定义头
    LPCWSTR headers = NULL;
    int hlen = 0;
    if (req->custom_header[0]) {
        char wheaders[4096];
        MultiByteToWideChar(CP_UTF8, 0, req->custom_header, -1, wheaders, 4096);
        headers = (LPCWSTR)wheaders;
        hlen = (int)wcslen((const wchar_t*)wheaders);
    }

    // 发送请求
    void *post = req->post_data[0] ? (void*)req->post_data : NULL;
    int postlen = req->post_data[0] ? (int)strlen(req->post_data) : 0;

    if (!WinHttpSendRequest(hRequest, headers, hlen, post, postlen, postlen, 0))
        goto cleanup;
    if (!WinHttpReceiveResponse(hRequest, NULL))
        goto cleanup;

    // 读取状态码
    DWORD sc = 0, sc_size = sizeof(sc);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        NULL, &sc, &sc_size, NULL);
    resp->status_code = (int)sc;
    const char *status_map[] = {"", "OK", "Found", "Not Found", "Error"};
    if (sc == 200) snprintf(resp->status_text, sizeof(resp->status_text), "OK");
    else if (sc == 404) snprintf(resp->status_text, sizeof(resp->status_text), "Not Found");
    else snprintf(resp->status_text, sizeof(resp->status_text), "HTTP %d", sc);

    // 读取全部响应体
    DWORD total_read = 0;
    while (total_read < TORK_HTTP_MAX_BODY - 1) {
        DWORD bytes_avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &bytes_avail) || bytes_avail == 0)
            break;
        DWORD to_read = bytes_avail;
        if (total_read + to_read >= TORK_HTTP_MAX_BODY - 1)
            to_read = TORK_HTTP_MAX_BODY - 1 - total_read;
        DWORD actually_read = 0;
        WinHttpReadData(hRequest, resp->body + total_read, to_read, &actually_read);
        if (actually_read == 0) break;
        total_read += actually_read;
    }
    resp->body[total_read] = 0;
    resp->body_len = (int)total_read;
    resp->success = 1;
    ret = 1;

cleanup:
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    if (!ret && !resp->success) {
        resp->success = 0;
        snprintf(resp->error, sizeof(resp->error), "WinHTTP error %lu", GetLastError());
    }
    return ret;
}

#else
// ==================== Linux 实现 ====================

static int socket_request(const tork_http_request_t *req, tork_http_response_t *resp) {
    char host[256], path[2048];
    int port;
    url_parse(req->url, host, sizeof(host), path, sizeof(path), &port);

    // DNS 解析
    struct hostent *he = gethostbyname(host);
    if (!he) {
        snprintf(resp->error, sizeof(resp->error), "DNS: %s", hstrerror(h_errno));
        return 0;
    }

    // 创建 socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        snprintf(resp->error, sizeof(resp->error), "socket() failed");
        return 0;
    }

    // 超时
    int timeout = (req->timeout_sec > 0 ? req->timeout_sec : TORK_HTTP_TIMEOUT);
    struct timeval tv = {timeout, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // 连接
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        snprintf(resp->error, sizeof(resp->error), "connect() failed");
        close(sock);
        return 0;
    }

    // 构造 HTTP 请求
    char request[8192];
    const char *method_str[] = {"GET", "POST", "PUT", "DELETE"};
    int n = snprintf(request, sizeof(request),
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: TORK/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n",
        method_str[req->method], path, host);

    if (req->custom_header[0])
        n += snprintf(request + n, sizeof(request) - n, "%s\r\n", req->custom_header);

    if (req->post_data[0]) {
        n += snprintf(request + n, sizeof(request) - n,
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n",
            strlen(req->post_data));
    }
    n += snprintf(request + n, sizeof(request) - n, "\r\n");
    if (req->post_data[0])
        n += snprintf(request + n, sizeof(request) - n, "%s", req->post_data);

    // 发送
    if (send(sock, request, n, 0) != n) {
        snprintf(resp->error, sizeof(resp->error), "send() failed");
        close(sock);
        return 0;
    }

    // 接收
    int total = 0;
    while (total < TORK_HTTP_MAX_BODY - 1) {
        int r = recv(sock, resp->body + total, TORK_HTTP_MAX_BODY - 1 - total, 0);
        if (r <= 0) break;
        total += r;
    }
    close(sock);
    resp->body[total] = 0;
    resp->body_len = total;

    // 解析状态码
    if (strncmp(resp->body, "HTTP/1.", 7) == 0) {
        int sc = 0;
        sscanf(resp->body + 9, "%d", &sc);
        resp->status_code = sc;
        // 提取状态文本
        char *sl = strchr(resp->body, '\r');
        if (sl) { int len = (int)(sl - resp->body - 13); if (len > 0) { if (len > 63) len = 63; strncpy(resp->status_text, resp->body + 13, len); resp->status_text[len] = 0; } }
        // 分离 body
        char *hdr_end = strstr(resp->body, "\r\n\r\n");
        if (hdr_end) {
            int body_start = (int)(hdr_end - resp->body + 4);
            int body_len = total - body_start;
            if (body_len > 0 && body_len < TORK_HTTP_MAX_BODY) {
                memmove(resp->body, resp->body + body_start, body_len);
                resp->body[body_len] = 0;
                resp->body_len = body_len;
            }
        }
    } else {
        resp->status_code = 0;
    }

    resp->success = 1;
    return 1;
}
#endif

// ==================== 公共 API 实现 ====================

void tork_http_request(const tork_http_request_t *req, tork_http_response_t *resp) {
    if (!req || !resp) return;
    memset(resp, 0, sizeof(*resp));
    resp->status_code = 0;
    resp->success = 0;

    clock_t start = clock();
#ifdef _WIN32
    winhttp_request(req, resp);
#else
    socket_request(req, resp);
#endif
    resp->elapsed_sec = (double)(clock() - start) / CLOCKS_PER_SEC;

    if (!resp->success && !resp->error[0])
        snprintf(resp->error, sizeof(resp->error), "request failed");
}

const char* tork_http_get(const char *url, tork_http_response_t *resp) {
    tork_http_request_t req;
    memset(&req, 0, sizeof(req));
    req.method = HTTP_GET;
    snprintf(req.url, sizeof(req.url), "%s", url);
    req.timeout_sec = TORK_HTTP_TIMEOUT;
    tork_http_request(&req, resp);
    return resp->success ? resp->body : NULL;
}

const char* tork_http_post_json(const char *url, const char *json_data,
                                tork_http_response_t *resp) {
    tork_http_request_t req;
    memset(&req, 0, sizeof(req));
    req.method = HTTP_POST;
    snprintf(req.url, sizeof(req.url), "%s", url);
    snprintf(req.post_data, sizeof(req.post_data), "%s", json_data);
    snprintf(req.custom_header, sizeof(req.custom_header),
             "Content-Type: application/json");
    req.timeout_sec = TORK_HTTP_TIMEOUT;
    tork_http_request(&req, resp);
    return resp->success ? resp->body : NULL;
}

const char* tork_http_public_ip(char *buf, int bufsize) {
    if (!buf || bufsize <= 0) return "unknown";
    tork_http_response_t resp;
    const char *services[] = {
        "http://api.ipify.org",
        "http://icanhazip.com",
        "http://ifconfig.me/ip",
        NULL
    };
    for (int i = 0; services[i]; i++) {
        if (tork_http_get(services[i], &resp)) {
            // 清理空白字符
            int len = resp.body_len;
            while (len > 0 && (resp.body[len-1] == '\n' || resp.body[len-1] == '\r' || resp.body[len-1] == ' '))
                len--;
            resp.body[len] = 0;
            snprintf(buf, bufsize, "%s", resp.body);
            return buf;
        }
    }
    snprintf(buf, bufsize, "unknown");
    return buf;
}

void tork_http_fetch_title(const char *html, char *title, int titlesize) {
    if (!html || !title || titlesize <= 0) return;
    title[0] = 0;
    const char *t = strstr(html, "<title>");
    if (!t) { t = strstr(html, "<TITLE>"); if (!t) return; }
    t += 7;
    const char *e = strstr(t, "</title>");
    if (!e) { e = strstr(t, "</TITLE>"); if (!e) return; }
    int len = (int)(e - t);
    if (len > titlesize - 1) len = titlesize - 1;
    strncpy(title, t, len);
    title[len] = 0;
}

int tork_http_ping(const char *url, int timeout_sec) {
    tork_http_request_t req;
    memset(&req, 0, sizeof(req));
    req.method = HTTP_GET;
    snprintf(req.url, sizeof(req.url), "%s", url);
    req.timeout_sec = timeout_sec > 0 ? timeout_sec : 5;
    tork_http_response_t resp;
    tork_http_request(&req, &resp);
    return (resp.success && resp.status_code == 200) ? 1 : 0;
}
