#include "tork_bitnet.h"
#include "../network/tork_http.h"
#include "../engine/tork_jsmn.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#endif

/* ── 默认参数 ────────────────────────────────────────────── */
#define BITNET_DEFAULT_N_PREDICT  256
#define BITNET_DEFAULT_TEMP       0.7f
#define BITNET_START_TIMEOUT      30     /* 等待服务就绪最大秒数 */
#define BITNET_POLL_INTERVAL_MS   500    /* 轮询间隔 */

/* ── 服务就绪检查 ────────────────────────────────────────── */
static int _ping_server(const char *host, int port) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/health", host, port);

    tork_http_response_t resp;
    memset(&resp, 0, sizeof(resp));

    const char *body = tork_http_get(url, &resp);
    if (body && resp.status_code == 200) return 1;
    return 0;
}

/* ── 提取 JSON 响应中的 "content" 字段 ──────────────────── */
static int _extract_content(const char *json, char *out, int out_size) {
    if (!json || !out) return -1;

    /* 用 jsmn 解析 */
    jsmn_parser parser;
    jsmntok_t tokens[64];
    jsmn_init(&parser);

    int num_tokens = jsmn_parse(&parser, json, strlen(json), tokens, 64);
    if (num_tokens < 2) {
        /* 简单的字符串搜索 fallback */
        const char *key = "\"content\":\"";
        const char *start = strstr(json, key);
        if (!start) return -1;
        start += strlen(key);

        const char *end = strchr(start, '"');
        if (!end) return -1;

        int len = (int)(end - start);
        if (len >= out_size) len = out_size - 1;
        memcpy(out, start, len);
        out[len] = '\0';

        /* 处理转义字符 */
        for (int i = 0; i < len; i++) {
            if (out[i] == '\\' && i + 1 < len) {
                if (out[i + 1] == 'n') { out[i] = '\n'; memmove(out + i + 1, out + i + 2, len - i - 2); len--; }
                else if (out[i + 1] == 't') { out[i] = '\t'; memmove(out + i + 1, out + i + 2, len - i - 2); len--; }
                else if (out[i + 1] == '"') { memmove(out + i, out + i + 1, len - i); len--; }
                else if (out[i + 1] == '\\') { memmove(out + i, out + i + 1, len - i); len--; }
            }
        }
        return len;
    }

    /* jsmn 解析成功：遍历找 "content" 字段 */
    for (int i = 1; i < num_tokens; i++) {
        if (tokens[i].type == JSMN_STRING) {
            int key_len = tokens[i].end - tokens[i].start;
            if (key_len == 7 && strncmp(json + tokens[i].start, "content", 7) == 0) {
                /* 下一个 token 应该是 content 的值 */
                if (i + 1 < num_tokens && tokens[i + 1].type == JSMN_STRING) {
                    int val_len = tokens[i + 1].end - tokens[i + 1].start;
                    if (val_len >= out_size) val_len = out_size - 1;
                    memcpy(out, json + tokens[i + 1].start, val_len);
                    out[val_len] = '\0';
                    return val_len;
                }
                i++;
            }
        }
    }
    return -1;
}

/* ── 初始化 ────────────────────────────────────────────── */
void tork_bitnet_init(tork_bitnet_bridge_t *bridge, const char *model_path) {
    memset(bridge, 0, sizeof(tork_bitnet_bridge_t));
    bridge->state = TORK_BITNET_STOPPED;
    bridge->port = TORK_BITNET_DEFAULT_PORT;
    strncpy(bridge->host, TORK_BITNET_DEFAULT_HOST, sizeof(bridge->host) - 1);
    if (model_path) {
        strncpy(bridge->model_path, model_path, TORK_BITNET_MAX_MODEL - 1);
        bridge->model_path[TORK_BITNET_MAX_MODEL - 1] = '\0';
    }
    bridge->server_pid = -1;
}

/* ── 启动服务 ────────────────────────────────────────────── */
int tork_bitnet_start(tork_bitnet_bridge_t *bridge, int port) {
    if (bridge->state == TORK_BITNET_RUNNING) return 0;
    if (port > 0) bridge->port = port;

    /* 先检查是否已有服务在运行 */
    if (_ping_server(bridge->host, bridge->port)) {
        bridge->state = TORK_BITNET_RUNNING;
        printf("[bitnet] 发现已有 BitNet 服务运行在 %s:%d\n",
               bridge->host, bridge->port);
        return 0;
    }

    /* 没有模型路径，无法启动 */
    if (bridge->model_path[0] == '\0') {
        snprintf(bridge->model_path, sizeof(bridge->model_path) - 1,
                 "models/bitnet-2b-q4_0.gguf");
    }

    bridge->state = TORK_BITNET_STARTING;
    printf("[bitnet] 启动 BitNet 服务...\n");
    printf("[bitnet]   模型: %s\n", bridge->model_path);
    printf("[bitnet]   端口: %s:%d\n", bridge->host, bridge->port);

    /* 构建启动命令 */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "build/bin/llama-server -m \"%s\" -c 2048 -t 2 -ngl 0 "
             "--host %s --port %d -cb",
             bridge->model_path, bridge->host, bridge->port);

#ifdef _WIN32
    /* Windows: 用 CreateProcess 后台启动 */
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    char cmdline[2048];
    snprintf(cmdline, sizeof(cmdline), "cmd.exe /c start /B %s", cmd);

    if (CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        bridge->server_pid = (int)pi.dwProcessId;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        printf("[bitnet] 启动失败 (CreateProcess): %lu\n", GetLastError());
        bridge->state = TORK_BITNET_ERROR;
        return -1;
    }
#else
    /* Linux: fork + exec */
    pid_t pid = fork();
    if (pid == 0) {
        /* 子进程 */
        execlp("build/bin/llama-server", "llama-server",
               "-m", bridge->model_path,
               "-c", "2048", "-t", "2", "-ngl", "0",
               "--host", bridge->host,
               "--port", NULL);
        _exit(1);
    } else if (pid > 0) {
        bridge->server_pid = (int)pid;
    } else {
        bridge->state = TORK_BITNET_ERROR;
        return -1;
    }
#endif

    /* 轮询等待服务就绪 */
    int waited = 0;
    while (waited < BITNET_START_TIMEOUT * 1000 / BITNET_POLL_INTERVAL_MS) {
        if (_ping_server(bridge->host, bridge->port)) {
            bridge->state = TORK_BITNET_RUNNING;
            printf("[bitnet] BitNet 服务就绪 (PID %d)\n", bridge->server_pid);
            return 0;
        }
#ifdef _WIN32
        Sleep(BITNET_POLL_INTERVAL_MS);
#else
        usleep(BITNET_POLL_INTERVAL_MS * 1000);
#endif
        waited++;
    }

    printf("[bitnet] 启动超时 (%ds)，服务未就绪\n", BITNET_START_TIMEOUT);
    bridge->state = TORK_BITNET_ERROR;
    return -1;
}

/* ── 就绪检查 ────────────────────────────────────────────── */
int tork_bitnet_is_ready(tork_bitnet_bridge_t *bridge) {
    if (bridge->state != TORK_BITNET_RUNNING) return 0;
    return _ping_server(bridge->host, bridge->port);
}

/* ── 单次查询 ────────────────────────────────────────────── */
int tork_bitnet_query(tork_bitnet_bridge_t *bridge,
                      const char *prompt,
                      tork_bitnet_result_t *result) {
    memset(result, 0, sizeof(tork_bitnet_result_t));

    if (bridge->state != TORK_BITNET_RUNNING) {
        if (!tork_bitnet_is_ready(bridge)) {
            snprintf(result->error, sizeof(result->error),
                     "BitNet 服务未就绪");
            return -1;
        }
        bridge->state = TORK_BITNET_RUNNING;
    }

    /* 构建请求 URL */
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/completion", bridge->host, bridge->port);

    /* 构建 JSON 请求体 */
    char json_body[TORK_BITNET_MAX_PROMPT + 256];
    snprintf(json_body, sizeof(json_body),
             "{\"prompt\":\"%s\",\"n_predict\":%d,\"temperature\":%.1f,"
             "\"stop\":[\"</s>\",\"\\n\\n\"]}",
             prompt, BITNET_DEFAULT_N_PREDICT, BITNET_DEFAULT_TEMP);

    /* 发送 HTTP POST 请求 */
    tork_http_response_t resp;
    memset(&resp, 0, sizeof(resp));

    const char *body = tork_http_post_json(url, json_body, &resp);
    result->elapsed_sec = (float)resp.elapsed_sec;

    if (!body || resp.status_code != 200) {
        result->success = 0;
        snprintf(result->error, sizeof(result->error),
                 "HTTP %d: %s", resp.status_code,
                 resp.error[0] ? resp.error : "unknown error");
        return -1;
    }

    /* 从 JSON 响应中提取 content */
    int len = _extract_content(body, result->response, TORK_BITNET_MAX_RESPONSE);
    if (len < 0) {
        result->success = 0;
        snprintf(result->error, sizeof(result->error),
                 "JSON 解析失败: 未找到 content 字段");
        return -1;
    }

    result->response_len = len;
    result->success = 1;

    /* 估算 token 数 (约每 4 字符 1 token) */
    result->tokens_generated = len / 4;
    if (result->tokens_generated < 1) result->tokens_generated = 1;

    bridge->total_queries++;
    bridge->total_tokens += result->tokens_generated;

    return 0;
}

/* ── 流式查询 ────────────────────────────────────────────── */
int tork_bitnet_query_stream(tork_bitnet_bridge_t *bridge,
                             const char *prompt,
                             tork_bitnet_token_cb callback,
                             void *userdata) {
    /* 简化版：先用非流式查询，然后逐字符调用回调 */
    tork_bitnet_result_t result;
    int ret = tork_bitnet_query(bridge, prompt, &result);
    if (ret != 0 || !result.success) return ret;

    /* 逐 token 回调 (空格分隔的单词作为 token 近似) */
    char token_buf[256];
    int ti = 0;
    for (int i = 0; i < result.response_len && result.response[i]; i++) {
        token_buf[ti++] = result.response[i];
        if (result.response[i] == ' ' || i == result.response_len - 1) {
            token_buf[ti] = '\0';
            if (callback) callback(token_buf, userdata);
            ti = 0;
        }
    }
    return 0;
}

/* ── 停止服务 ────────────────────────────────────────────── */
void tork_bitnet_stop(tork_bitnet_bridge_t *bridge) {
    if (bridge->server_pid > 0) {
        printf("[bitnet] 停止服务 (PID %d)\n", bridge->server_pid);

#ifdef _WIN32
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, bridge->server_pid);
        if (hProc) {
            TerminateProcess(hProc, 0);
            CloseHandle(hProc);
        }
#else
        kill(bridge->server_pid, SIGTERM);
        waitpid(bridge->server_pid, NULL, 0);
#endif

        bridge->server_pid = -1;
    }
    bridge->state = TORK_BITNET_STOPPED;
    printf("[bitnet] 服务已停止\n");
}

/* ── 摘要报告 ────────────────────────────────────────────── */
void tork_bitnet_summary(const tork_bitnet_bridge_t *bridge,
                         char *buf, int bufsize) {
    const char *state_str = "stopped";
    switch (bridge->state) {
        case TORK_BITNET_STOPPED:  state_str = "stopped";  break;
        case TORK_BITNET_STARTING: state_str = "starting"; break;
        case TORK_BITNET_RUNNING:  state_str = "running";  break;
        case TORK_BITNET_ERROR:    state_str = "error";    break;
    }

    snprintf(buf, bufsize,
             "BitNet b1.58 三值大脑\n"
             "  状态:      %s\n"
             "  模型:      %s\n"
             "  服务:      %s:%d (PID %d)\n"
             "  查询次数:  %d\n"
             "  生成Token: %d\n"
             "  三值架构:  {-1, 0, +1} 权重, 能耗降低 82%%\n"
             "  推理加速:  CPU 2.37x~6.17x vs 传统 FP16\n"
             "  100B 模型: 单 CPU 即可运行 (5-7 tok/s)\n",
             state_str,
             bridge->model_path[0] ? bridge->model_path : "(未配置)",
             bridge->host, bridge->port, bridge->server_pid,
             bridge->total_queries, bridge->total_tokens);
}
