#define _GNU_SOURCE
#include "dispatch.h"
#include "torkd.h"
#include "task.h"
#include "auditor.h"
#include "codegen.h"
#include "query.h"
#include "soul_access.h"
#include "scheduler.h"
#include "../sandbox/sandbox.h"
#include "../learning/watcher.h"
/* snapshot stats from monitor */
#include "../learning/experience.h"
#include "../learning/branch.h"
#include "../learning/self_build.h"
#include "../learning/mutation_guide.h"
#include "../learning/mentor.h"
#include "torkd_binary.h"

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
#include <pwd.h>

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

/* ── SO_PEERCRED 认证: 验证连接方 UID 与引擎进程一致 ─────── */
static int peercred_check(int client_fd) {
    struct ucred cred;
    socklen_t cred_len = sizeof(cred);
    if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) < 0) {
        return -1;
    }
    if (cred.uid != getuid()) {
        return -1;
    }
    return 0;
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

/* ── 二进制帧处理 ───────────────────────────────────────── */
static int handle_binary_frame(int client_fd, const uint8_t *buf, size_t len) {
    const uint8_t *payload;
    uint16_t plen;
    uint8_t opcode = bf_decode_req(buf, len, &payload, &plen);
    if (opcode == 0) return -1;

    uint8_t resp[BF_BUF_SIZE];
    int rlen;

    switch (opcode) {
    case BF_PING: {
        const char *ok = "ok";
        rlen = bf_encode_resp(resp, sizeof(resp), BF_PING, 0, ok, 2);
        break;
    }
    case BF_STATUS: {
        /* 返回短状态文本 */
        const char *st = g_soul ? "up" : "init";
        rlen = bf_encode_resp(resp, sizeof(resp), BF_STATUS, 0, st, strlen(st));
        break;
    }
    case BF_STATE: {
        /* 返回 JSON 状态 — 复用现有 state 逻辑但通过二进制帧 */
        char text_resp[TORKD_MAX_MSG * 2];
        // generate_state_json
        const tork_instinct_t *inst = sched_last_instinct();
        const mentor_state_t *ms = mentor_get_state();
        uint8_t ms_cw, ms_lw, ms_aw;
        mentor_decision_weights(&ms_cw, &ms_lw, &ms_aw);
        snprintf(text_resp, sizeof(text_resp),
            "{\"heartbeat\":{\"tick\":%u,\"hw_stress\":%u,\"drive\":%d},"
            "\"learning\":{\"tln_a\":%d,\"tln_m\":%d,\"tln_e\":%d,\"exp\":%u,\"pat\":%.2f},"
            "\"evolution\":{\"gen\":%u,\"mut\":%u,\"score\":%u}}",
            g_soul ? soul_tick(g_soul) : 0,
            g_soul ? soul_hw_stress(g_soul) : 0,
            g_soul ? soul_drive(g_soul) : 0,
            g_soul ? (int)soul_tln_action(g_soul) : 0,
            g_soul ? (int)soul_tln_modify(g_soul) : 0,
            g_soul ? (int)soul_tln_explore(g_soul) : 0,
            g_soul ? (unsigned)soul_experience_count(g_soul) : 0,
            ms->pattern_confidence,
            g_soul ? soul_gen_count(g_soul) : 0,
            g_soul ? (unsigned)soul_mutation_count(g_soul) : 0,
            g_soul ? (unsigned)soul_best_score(g_soul) : 0);
        uint16_t tlen = strlen(text_resp);
        rlen = bf_encode_resp(resp, sizeof(resp), BF_STATE, 0, text_resp, tlen);
        break;
    }
    case BF_SOUL: {
        if (!g_soul) {
            const char *e = "no_soul";
            rlen = bf_encode_resp(resp, sizeof(resp), BF_ERR, 1, e, 7);
        } else {
            char hexdump[512];
            int pos = 0;
            for (int i = 0; i < SOUL_SIZE && pos < (int)sizeof(hexdump) - 4; i++)
                pos += snprintf(hexdump + pos, sizeof(hexdump) - pos, "%02x", g_soul->buf[i]);
            rlen = bf_encode_resp(resp, sizeof(resp), BF_SOUL, 0, hexdump, pos);
        }
        break;
    }
    case BF_EXEC: {
        if (!payload || plen == 0) {
            const char *e = "empty";
            rlen = bf_encode_resp(resp, sizeof(resp), BF_ERR, 1, e, 5);
        } else {
            char cmd[512];
            uint16_t clen = plen < (int)sizeof(cmd)-1 ? plen : sizeof(cmd)-1;
            memcpy(cmd, payload, clen);
            cmd[clen] = '\0';
            dispatch_output_t dout = dispatch_quick(DISP_EXEC_CMD, cmd, NULL);
            rlen = bf_encode_resp(resp, sizeof(resp), BF_EXEC, 0, dout.output, strlen(dout.output));
        }
        break;
    }
    case BF_QUERY: {
        if (!payload || plen == 0) {
            const char *e = "empty";
            rlen = bf_encode_resp(resp, sizeof(resp), BF_ERR, 1, e, 5);
        } else {
            char question[512];
            uint16_t qlen = plen < (int)sizeof(question)-1 ? plen : sizeof(question)-1;
            memcpy(question, payload, qlen);
            question[qlen] = '\0';
            char ans[TORKD_MAX_MSG];
            query_handle(question, g_soul, ans, sizeof(ans));
            rlen = bf_encode_resp(resp, sizeof(resp), BF_QUERY, 0, ans, strlen(ans));
        }
        break;
    }
    case BF_TASKS: {
        char tstat[128];
        snprintf(tstat, sizeof(tstat),
            "{\"p\":%d,\"a\":%d,\"c\":%u,\"f\":%u}",
            task_pending_count(), task_active_count(),
            task_total_completed(), task_total_failed());
        rlen = bf_encode_resp(resp, sizeof(resp), BF_TASKS, 0, tstat, strlen(tstat));
        break;
    }
    case BF_MENTOR: {
        const mentor_state_t *ms = mentor_get_state();
        uint8_t cw, lw, aw;
        mentor_decision_weights(&cw, &lw, &aw);
        char mstr[256];
        snprintf(mstr, sizeof(mstr),
            "{\"s\":\"%s\",\"cw\":%d,\"lw\":%d,\"aw\":%d,\"pc\":%.2f}",
            mentor_stage_name(ms->stage), cw, lw, aw, ms->pattern_confidence);
        rlen = bf_encode_resp(resp, sizeof(resp), BF_MENTOR, 0, mstr, strlen(mstr));
        break;
    }
    case BF_DISPATCH: {
        uint32_t dt, ds, df;
        dispatch_get_stats(&dt, &ds, &df);
        char dstr[96];
        snprintf(dstr, sizeof(dstr),
            "{\"t\":%u,\"s\":%u,\"f\":%u}", dt, ds, df);
        rlen = bf_encode_resp(resp, sizeof(resp), BF_DISPATCH, 0, dstr, strlen(dstr));
        break;
    }
    case BF_CUSTOM:
    case BF_RAW:
    default: {
        /* 回退到文本处理：构造文本命令 */
        char text_cmd[TORKD_MAX_MSG];
        uint16_t cmd_len = plen < (int)sizeof(text_cmd)-1 ? plen : sizeof(text_cmd)-1;
        if (payload && cmd_len > 0) {
            memcpy(text_cmd, payload, cmd_len);
            text_cmd[cmd_len] = '\0';
        } else {
            text_cmd[0] = '\0';
        }
        /* 这里不能递归调 handle_client，直接在 inline 处理几个通用命令 */
        char text_resp[TORKD_MAX_MSG * 2];
        if (strcmp(text_cmd, "exit") == 0 || strcmp(text_cmd, "quit") == 0) {
            const char *bye = "bye";
            rlen = bf_encode_resp(resp, sizeof(resp), BF_CUSTOM, 0, bye, 3);
            full_write(client_fd, resp, rlen);
            close(client_fd);
            return 0;
        } else {
            snprintf(text_resp, sizeof(text_resp), "unknown binary cmd: %s", text_cmd);
            rlen = bf_encode_resp(resp, sizeof(resp), BF_ERR, 1, text_resp, strlen(text_resp));
        }
        break;
    }
    }

    if (rlen > 0) {
        full_write(client_fd, resp, rlen);
    }
    return 0;
}

/* ── 文本协议处理 (兼容原有) ─────────────────────────────── */
static void handle_client(int client_fd) {
    char buf[TORKD_MAX_MSG];
    memset(buf, 0, sizeof(buf));

        /* 自动检测: 二进制帧 (首字节 0xBF) 或文本协议 */
    if (n > 0 && bf_is_binary((const uint8_t*)buf, (size_t)n)) {
        handle_binary_frame(client_fd, (const uint8_t*)buf, (size_t)n);
        close(client_fd);
        return;
    }

    /* Blocking read for client — we have 2s timeout on the socket */
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) { close(client_fd); return; }
    if (buf[n-1] == '\n') buf[n-1] = '\0';
    
    char response[TORKD_MAX_MSG * 2];

    /* 判断是否为敏感命令 (需要 SO_PEERCRED 认证) */
    int need_auth = (strcmp(buf, "soul") == 0 || strcmp(buf, "灵魂") == 0 ||
                     strcmp(buf, "state") == 0 ||
                     strncmp(buf, "exec:", 5) == 0 ||
                     strncmp(buf, "audit:", 6) == 0 ||
                     strncmp(buf, "codegen:", 8) == 0 ||
                     strncmp(buf, "task:", 5) == 0);
    if (need_auth && peercred_check(client_fd) < 0) {
        snprintf(response, sizeof(response), "{\"error\":\"permission denied\"}\n");
        full_write(client_fd, response, strlen(response));
        close(client_fd);
        return;
    }

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
                    int esc_size = (int)sizeof(response);
                    char *escaped_out = malloc(esc_size);
                    if (escaped_out) {
                        json_escape_buf(entry.output, escaped_out, esc_size);
                        snprintf(response, sizeof(response),
                            "{\"task_id\":%u,\"status\":\"%s\",\"exit_code\":%d,\"output\":\"%s\"}\n",
                            tid, status_str, entry.exit_code, escaped_out);
                        free(escaped_out);
                    } else {
                        snprintf(response, sizeof(response),
                            "{\"task_id\":%u,\"status\":\"%s\",\"exit_code\":%d}\n",
                            tid, status_str, entry.exit_code);
                    }
                } else {
                    snprintf(response, sizeof(response), "{\"task_id\":%u,\"status\":\"error\"}\n", tid);
                }
            } else {
                snprintf(response, sizeof(response), "{\"task_id\":%u,\"status\":\"%s\"}\n", tid, status_str);
            }
        }
    } else if (strcmp(buf, "state") == 0) {
        /* state — Aurora JSON: heartbeat/instinct/learning/evolution */
        const tork_instinct_t *inst = sched_last_instinct();
        const mentor_state_t *ms = mentor_get_state();
        uint8_t ms_cw, ms_lw, ms_aw;
        mentor_decision_weights(&ms_cw, &ms_lw, &ms_aw);
        snprintf(response, sizeof(response),
            "{\"heartbeat\":{\"tick\":%u,\"hw_stress\":%u,\"heartbeat_ms\":%u,\"drive\":%d},"
            "\"instinct\":{\"fear\":%.3f,\"desire\":%.3f,\"curiosity\":%.3f},"
            "\"learning\":{\"tln_action\":%d,\"tln_modify\":%d,\"tln_explore\":%d,\"tln_energy\":%d,"
            "\"experience_count\":%u,\"mcts_iterations\":%u,\"pattern_confidence\":%.2f,"
            "\"mentor_stage\":\"%s\"},"
            "\"evolution\":{\"gen_count\":%u,\"mutation_count\":%u,\"best_score\":%u,"
            "\"mentor_cloud_weight\":%d,\"mentor_local_weight\":%d,\"mentor_auto_weight\":%d}}\n",
            g_soul ? soul_tick(g_soul) : 0,
            g_soul ? soul_hw_stress(g_soul) : 0,
            g_soul ? (unsigned)soul_heartbeat_ms(g_soul) : 100,
            g_soul ? soul_drive(g_soul) : 0,
            inst->fear, inst->desire, inst->curiosity,
            g_soul ? (int)soul_tln_action(g_soul) : 0,
            g_soul ? (int)soul_tln_modify(g_soul) : 0,
            g_soul ? (int)soul_tln_explore(g_soul) : 0,
            g_soul ? (int)soul_tln_energy(g_soul) : 0,
            g_soul ? (unsigned)soul_experience_count(g_soul) : 0,
            g_soul ? (unsigned)soul_mcts_iterations(g_soul) : 0,
            ms->pattern_confidence,
            mentor_stage_name(ms->stage),
            g_soul ? soul_gen_count(g_soul) : 0,
            g_soul ? (unsigned)soul_mutation_count(g_soul) : 0,
            g_soul ? (unsigned)soul_best_score(g_soul) : 0,
            ms_cw, ms_lw, ms_aw);
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


/* ── 二进制帧查询 ────────────────────────────────────────── */
int torkd_query_binary(uint8_t opcode, const void *payload, uint16_t plen,
                        uint8_t *resp_buf, size_t resp_cap,
                        uint16_t *out_status) {
    if (!resp_buf || resp_cap < BF_RESP_HEADER) return -1;

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

    struct timeval tv;
    tv.tv_sec = 5; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* 编码并发送二进制帧 */
    uint8_t req[BF_BUF_SIZE];
    int req_len = bf_encode_req(req, sizeof(req), opcode, payload, plen);
    if (req_len <= 0) { close(fd); return -1; }

    full_write(fd, req, req_len);

    /* 读取二进制响应 */
    memset(resp_buf, 0, resp_cap);
    ssize_t n = read(fd, resp_buf, resp_cap - 1);
    if (n < 0) { close(fd); return -1; }

    /* 解析响应头 */
    if ((size_t)n < BF_RESP_HEADER || resp_buf[0] != BF_MAGIC) {
        close(fd);
        return -1;
    }

    if (out_status) {
        *out_status = ((uint16_t)resp_buf[2] << 8) | resp_buf[3];
    }

    close(fd);
    return (int)n;  /* 返回响应总长度 */
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
