#define _GNU_SOURCE
#include "dispatch.h"
#include "torkd.h"
#include "task.h"
#include "auditor.h"
#include "codegen.h"
#include "query.h"
#include "soul_access.h"
#include "../sandbox/sandbox.h"
#include "../learning/watcher.h"
/* snapshot stats from monitor */
#include "../learning/experience.h"
#include "../learning/branch.h"
#include "../learning/self_build.h"
#include "../learning/mutation_guide.h"
#include "../learning/mentor.h"

/* JSON 转义辅助 */
static int json_escape_buf(const char *src, char *dst, int dst_size) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 2; i++) {
        switch (src[i]) {
        case '"':  dst[j++] = '\\'; dst[j++] = '"';  break;
        case '\\': dst[j++] = '\\'; dst[j++] = '\\'; break;
        case '\n': dst[j++] = '\\'; dst[j++] = 'n';  break;
        case '\r': dst[j++] = '\\'; dst[j++] = 'r';  break;
        case '\t': dst[j++] = '\\'; dst[j++] = 't';  break;
        default:   dst[j++] = src[i]; break;
        }
    }
    dst[j] = '\0';
    return j;
}

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

static int g_server_fd = -1;
static int g_started = 0;
static soul_t *g_soul = NULL;
static void full_write(int fd, const void *buf, size_t len) {
    const char *p = buf;
    while (len > 0) {
        ssize_t w = write(fd, p, len);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (w == 0) break;
        p += w; len -= w;
    }
}

/* ── 初始化: 创建 socket 并开始监听 ────────────────────── */
int torkd_init(void *vsoul) {
    if (g_started) return 0;
    
    unlink(TORKD_SOCKET_PATH);
    
    g_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_server_fd < 0) return -1;
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, TORKD_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (bind(g_server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(g_server_fd);
        g_server_fd = -1;
        return -1;
    }
    
    chmod(TORKD_SOCKET_PATH, 0600);
    
    if (listen(g_server_fd, TORKD_BACKLOG) < 0) {
        close(g_server_fd);
        g_server_fd = -1;
        unlink(TORKD_SOCKET_PATH);
        return -1;
    }
    
    /* Non-blocking */
    int flags = fcntl(g_server_fd, F_GETFL, 0);
    if (flags >= 0) fcntl(g_server_fd, F_SETFL, flags | O_NONBLOCK);
    
    signal(SIGPIPE, SIG_IGN);
    g_soul = (soul_t *)vsoul;
    g_started = 1;
    
    printf("  TORKD: listening on %s (non-blocking, integrated)\n", TORKD_SOCKET_PATH);
    return 0;
}

/* ── Dispatch helper ───────────────────────────────────────── */
static dispatch_output_t dispatch_quick(int action, const char *input,
                                         const char *func_name) {
    dispatch_input_t din;
    memset(&din, 0, sizeof(din));
    din.action      = action;
    din.input       = input;
    din.func_name   = func_name;
    din.timeout_sec = 30;
    din.tick        = g_soul ? soul_tick(g_soul) : 0;
    din.hw_stress   = g_soul ? soul_hw_stress(g_soul) : 0;
    din.drive       = g_soul ? soul_drive(g_soul) : 0;
    din.gen_count   = g_soul ? soul_gen_count(g_soul) : 0;
    return tork_dispatch(&din);
}

/* ── 每 tick 调用: 接受新连接 + 处理 ──────────────────────── */
static void handle_client(int client_fd) {
    char buf[TORKD_MAX_MSG];
    memset(buf, 0, sizeof(buf));

    /* Blocking read for client — we have 2s timeout on the socket */
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) { close(client_fd); return; }
    if (buf[n-1] == '\n') buf[n-1] = '\0';
    
    char response[TORKD_MAX_MSG * 2];
    
    if (strcmp(buf, "ping") == 0) {
        snprintf(response, sizeof(response), "pong\n");
    } else if (strcmp(buf, "status") == 0 || strcmp(buf, "状态") == 0) {
        char sb_sum[256] = "";
        sb_summary(sb_sum, sizeof(sb_sum));

        int active_branches = br_active_count();
        uint32_t exp_cnt = exp_count();
        uint8_t ms_cw, ms_lw, ms_aw;
        uint32_t ds_total, ds_ok, ds_fail;
        mentor_decision_weights(&ms_cw, &ms_lw, &ms_aw);
        dispatch_get_stats(&ds_total, &ds_ok, &ds_fail);
        
        snprintf(response, sizeof(response),
            "TORK v3.17 师徒阶段 + TLN 反馈回路\n"
            "   心跳: %u tick | 驱动: %+d\n"
            "   压力: %u | 世代: %u\n"
            "   分支: %d 个活跃 | 经验: %u 条\n"
            "   快照: %d 层, 回滚 %d 次\n"
            "   能量: 模式 %d, 节流 %d%%\n"
            "   TLN: act=%+d mod=%+d exp=%+d nrg=%+d\n"
            "   师徒: %s (云=%d%% 本=%d%% 自=%d%%)\n"
            "   Dispatch: %u calls, %u ok, %u fail\n"
            "   %s\n",
            g_soul ? soul_tick(g_soul) : 0,
            g_soul ? soul_drive(g_soul) : 0,
            g_soul ? soul_hw_stress(g_soul) : 0,
            g_soul ? soul_gen_count(g_soul) : 6,
            active_branches, exp_cnt,
            0, 0,  /* snapshot stats */
            1, 0,   /* energy stats: mode=1, throttle=0 */
            g_soul ? (int)soul_tln_action(g_soul) : 0,
            g_soul ? (int)soul_tln_modify(g_soul) : 0,
            g_soul ? (int)soul_tln_explore(g_soul) : 0,
            g_soul ? (int)soul_tln_energy(g_soul) : 0,
            mentor_stage_name(mentor_get_stage()), ms_cw, ms_lw, ms_aw,
            ds_total, ds_ok, ds_fail,
            sb_sum);
    } else if (strcmp(buf, "exit") == 0 || strcmp(buf, "quit") == 0) {
        snprintf(response, sizeof(response), "bye\n");
        full_write(client_fd, response, strlen(response));
        close(client_fd);
        return;
    } else if (strncmp(buf, "exec:", 5) == 0) {
        const char *cmd = buf + 5;
        if (*cmd == '\0') {
            snprintf(response, sizeof(response), "{\"error\":\"empty command\"}\n");
        } else {
            dispatch_output_t dout = dispatch_quick(DISP_EXEC_CMD, cmd, NULL);
            int len = snprintf(response, sizeof(response), "%s\n", dout.output);
            if (len >= (int)sizeof(response))
                snprintf(response, sizeof(response), "{\"error\":\"response truncated (%d bytes)\"}\n", len);
        }
    } else if (strncmp(buf, "audit:", 6) == 0) {
        /* audit:<filepath>[:funcname] — 通过 dispatch 闭环审计 */
        const char *arg = buf + 6;
        char filepath[256] = "";
        char funcname[64] = "";
        const char *colon = strchr(arg, ':');
        if (colon) {
            int plen = (int)(colon - arg);
            if (plen >= (int)sizeof(filepath)) plen = sizeof(filepath) - 1;
            memcpy(filepath, arg, plen);
            filepath[plen] = '\0';
            snprintf(funcname, sizeof(funcname), "%s", colon + 1);
        } else {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
            strncpy(filepath, arg, sizeof(filepath) - 1);
#pragma GCC diagnostic pop
            filepath[sizeof(filepath) - 1] = '\0';
        }
        if (filepath[0] == '\0') {
            snprintf(response, sizeof(response), "{\"error\":\"empty path\"}\n");
        } else {
            dispatch_output_t dout = dispatch_quick(DISP_AUDIT_CODE, filepath, funcname[0] ? funcname : NULL);
            int len = snprintf(response, sizeof(response), "%s\n", dout.output);
            if (len >= (int)sizeof(response))
                snprintf(response, sizeof(response), "{\"error\":\"response truncated (%d bytes)\"}\n", len);
        }
    } else if (strncmp(buf, "codegen:", 8) == 0) {
        /* codegen:<action>:<template>
         * action = search | compile | bench
         * template = memcpy_byte_loop | memcpy_word_loop */
        const char *arg = buf + 8;
        const char *colon = strchr(arg, ':');
        char action[32] = "";
        char tmpl_name[64] = "";
        if (colon) {
            int alen = (int)(colon - arg);
            if (alen >= (int)sizeof(action)) alen = sizeof(action) - 1;
            memcpy(action, arg, alen);
            action[alen] = '\0';
            snprintf(tmpl_name, sizeof(tmpl_name), "%s", colon + 1);
        } else {
            snprintf(action, sizeof(action), "%s", arg);
        }

        if (tmpl_name[0] == '\0') {
            snprintf(response, sizeof(response),
                "{\"error\":\"usage: codegen:<search|compile|bench>:<template>\"}\n");
        } else if (strcmp(action, "search") == 0) {
            dispatch_output_t dout = dispatch_quick(DISP_CODEGEN_SEARCH, tmpl_name, NULL);
            int clen = snprintf(response, sizeof(response), "%s\n", dout.output);
            if (clen >= (int)sizeof(response))
                snprintf(response, sizeof(response), "{\"error\":\"response truncated\"}\n");
        } else if (strcmp(action, "compile") == 0) {
            dispatch_output_t dout = dispatch_quick(DISP_CODEGEN_COMPILE, tmpl_name, NULL);
            int clen = snprintf(response, sizeof(response), "%s\n", dout.output);
            if (clen >= (int)sizeof(response))
                snprintf(response, sizeof(response), "{\"error\":\"response truncated\"}\n");
        } else if (strcmp(action, "bench") == 0) {
            dispatch_output_t dout = dispatch_quick(DISP_CODEGEN_BENCH, tmpl_name, NULL);
            int clen = snprintf(response, sizeof(response), "%s\n", dout.output);
            if (clen >= (int)sizeof(response))
                snprintf(response, sizeof(response), "{\"error\":\"response truncated\"}\n");
        } else {
            snprintf(response, sizeof(response),
                "{\"error\":\"unknown codegen action '%s'\"}\n", action);
        }
    } else if (strncmp(buf, "task:", 5) == 0) {
        /* task:<type>:<input> — 提交异步任务
         * type = exec | analyze | audit
         * 返回 task_id */
        const char *arg = buf + 5;
        task_type_t ttype = TASK_UNKNOWN_TYPE;
        const char *input = NULL;
        if (strncmp(arg, "exec:", 5) == 0) { ttype = TASK_EXEC; input = arg + 5; }
        else if (strncmp(arg, "analyze:", 8) == 0) { ttype = TASK_ANALYZE; input = arg + 8; }
        else if (strncmp(arg, "audit:", 6) == 0) { ttype = TASK_AUDIT; input = arg + 6; }

        if (ttype == TASK_UNKNOWN_TYPE || !input || *input == '\0') {
            snprintf(response, sizeof(response), "{\"error\":\"usage: task:<exec|analyze|audit>:<input>\"}\n");
        } else {
            uint32_t tid = task_submit(ttype, input);
            if (tid > 0)
                snprintf(response, sizeof(response), "{\"task_id\":%u,\"status\":\"pending\"}\n", tid);
            else
                snprintf(response, sizeof(response), "{\"error\":\"task queue full\"}\n");
        }
    } else if (strncmp(buf, "result:", 7) == 0) {
        /* result:<task_id> — 查询任务结果 */
        uint32_t tid = (uint32_t)strtoul(buf + 7, NULL, 10);
        if (tid == 0) {
            snprintf(response, sizeof(response), "{\"error\":\"invalid task_id\"}\n");
        } else {
            task_status_t st = task_status(tid);
            const char *status_str = "not_found";
            if (st == TASK_PENDING) status_str = "pending";
            else if (st == TASK_RUNNING) status_str = "running";
            else if (st == TASK_DONE) status_str = "done";
            else if (st == TASK_FAILED) status_str = "failed";
            else if (st == TASK_CANCELLED) status_str = "cancelled";

            if (st == TASK_PENDING || st == TASK_RUNNING) {
                snprintf(response, sizeof(response), "{\"task_id\":%u,\"status\":\"%s\"}\n", tid, status_str);
            } else if (st == TASK_DONE || st == TASK_FAILED) {
                task_entry_t entry;
                if (task_result(tid, &entry) == 0) {
                    char escaped_out[sizeof(response)];
                    json_escape_buf(entry.output, escaped_out, sizeof(escaped_out));
                    snprintf(response, sizeof(response),
                        "{\"task_id\":%u,\"status\":\"%s\",\"exit_code\":%d,\"output\":\"%s\"}\n",
                        tid, status_str, entry.exit_code, escaped_out);
                } else {
                    snprintf(response, sizeof(response), "{\"task_id\":%u,\"status\":\"error\"}\n", tid);
                }
            } else {
                snprintf(response, sizeof(response), "{\"task_id\":%u,\"status\":\"%s\"}\n", tid, status_str);
            }
        }
    } else if (strcmp(buf, "soul") == 0 || strcmp(buf, "灵魂") == 0) {
        /* soul — dump raw soul as hex for Python parser */
        if (!g_soul) {
            snprintf(response, sizeof(response), "{\"error\":\"no soul\"}\n");
        } else {
            char *p = response;
            p += snprintf(p, sizeof(response) - (p - response), "0x");
            for (int i = 0; i < SOUL_SIZE && (p - response) < (ssize_t)sizeof(response) - 4; i++)
                p += snprintf(p, 3, "%02x", g_soul->buf[i]);
            p += snprintf(p, 2, "\n");
        }
    } else if (strcmp(buf, "tasks") == 0) {
        /* tasks — 查看任务队列状态 */
        snprintf(response, sizeof(response),
            "{\"pending\":%d,\"active\":%d,\"completed\":%u,\"failed\":%u}\n",
            task_pending_count(), task_active_count(),
            task_total_completed(), task_total_failed());
    } else if (strcmp(buf, "mentor") == 0 || strcmp(buf, "师徒") == 0) {
        /* mentor — 师徒阶段查询 */
        const mentor_state_t *ms = mentor_get_state();
        uint8_t cw, lw, aw;
        mentor_decision_weights(&cw, &lw, &aw);
        snprintf(response, sizeof(response),
            "{\"stage\":\"%s\",\"stage_id\":%d,\"cloud_weight\":%d,\"local_weight\":%d,\"auto_weight\":%d,"
            "\"cloud_queries\":%u,\"local_decisions\":%u,\"auto_mutations\":%u,"
            "\"pattern_conf\":%.2f,\"tln_cons\":%.2f}\n",
            mentor_stage_name(ms->stage), ms->stage,
            cw, lw, aw,
            ms->cloud_queries, ms->local_decisions, ms->autonomous_mutations,
            ms->pattern_confidence, ms->tln_consistency);
    } else if (strcmp(buf, "dispatch") == 0) {
        uint32_t dt, ds, df;
        dispatch_get_stats(&dt, &ds, &df);
        snprintf(response, sizeof(response),
            "{\"total_calls\":%u,\"success\":%u,\"fail\":%u,\"success_rate\":%.1f}\n",
            dt, ds, df, dt > 0 ? (float)ds / (float)dt * 100.0f : 0.0f);
    } else {
        /* Normal question */
        query_handle(buf, g_soul, response, sizeof(response));
        { size_t rlen = strlen(response); if (rlen + 1 < sizeof(response)) { response[rlen] = '\n'; response[rlen+1] = '\0'; } }
    }
    
    full_write(client_fd, response, strlen(response));
    close(client_fd);
}

void torkd_tick(void) {
    if (!g_started || g_server_fd < 0) return;
    
    /* Accept all pending connections */
    while (1) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept4(g_server_fd, (struct sockaddr*)&client_addr, &client_len, SOCK_CLOEXEC);
        if (client_fd < 0) break;  /* EAGAIN or EWOULDBLOCK → no more clients */
        
        /* Set 2s timeout for client communication */
        struct timeval tv;
        tv.tv_sec = 2; tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        
        handle_client(client_fd);
    }
}

/* ── 停止 ────────────────────────────────────────────────── */
void torkd_shutdown(void) {
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
    unlink(TORKD_SOCKET_PATH);
    g_started = 0;
    printf("  TORKD: shutdown\n");
}

/* ── 查询（连接已有 socket） ──────────────────────────────── */
int torkd_query(const char *question, char *response, int max_len) {
    if (!question || !response || max_len < 1) return -1;
    
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, TORKD_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    /* 5s send/receive timeout to avoid blocking indefinitely */
    struct timeval tv;
    tv.tv_sec = 5; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    char buf[TORKD_MAX_MSG];
    snprintf(buf, sizeof(buf), "%s\n", question);
    full_write(fd, buf, strlen(buf));
    
    memset(response, 0, max_len);
    ssize_t n = read(fd, response, max_len - 1);
    if (n < 0) n = 0;
    response[n] = '\0';
    
    close(fd);
    return 0;
}

int torkd_is_running(void) {
    struct stat st;
    if (stat(TORKD_SOCKET_PATH, &st) != 0) return 0;
    
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, TORKD_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    int ok = (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    close(fd);
    return ok;
}
