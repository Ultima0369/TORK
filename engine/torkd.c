#include "torkd.h"
#include "query.h"
#include "soul_access.h"
#include "../learning/watcher.h"
/* snapshot stats from monitor */


#include "../learning/experience.h"
#include "../learning/branch.h"
#include "../learning/self_build.h"
#include "../learning/mutation_guide.h"

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
    
    chmod(TORKD_SOCKET_PATH, 0666);
    
    if (listen(g_server_fd, TORKD_BACKLOG) < 0) {
        close(g_server_fd);
        g_server_fd = -1;
        unlink(TORKD_SOCKET_PATH);
        return -1;
    }
    
    /* Non-blocking */
    int flags = fcntl(g_server_fd, F_GETFL, 0);
    fcntl(g_server_fd, F_SETFL, flags | O_NONBLOCK);
    
    signal(SIGPIPE, SIG_IGN);
    g_soul = (soul_t *)vsoul;
    g_started = 1;
    
    printf("  TORKD: listening on %s (non-blocking, integrated)\\n", TORKD_SOCKET_PATH);
    return 0;
}

/* ── 每 tick 调用: 接受新连接 + 处理 ──────────────────────── */
static void handle_client(int client_fd) {
    char buf[TORKD_MAX_MSG];
    memset(buf, 0, sizeof(buf));
    
    /* Non-blocking read */
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) { close(client_fd); return; }
    if (buf[n-1] == '\n') buf[n-1] = '\0';
    
    char response[TORKD_MAX_MSG];
    
    if (strcmp(buf, "ping") == 0) {
        snprintf(response, sizeof(response), "pong\n");
    } else if (strcmp(buf, "status") == 0 || strcmp(buf, "状态") == 0) {
        char sb_sum[256] = "";
        sb_summary(sb_sum, sizeof(sb_sum));
        
        int active_branches = br_active_count();
        uint32_t exp_cnt = exp_count();
        
        snprintf(response, sizeof(response),
            "🥚 TORK v3.4 已集成\n"
            "   心跳: %u tick | 驱动: %+d\n"
            "   压力: %u | 世代: %u\n"
            "   分支: %d 个活跃 | 经验: %u 条\n"
            "   快照: %d 层, 回滚 %d 次\n"
            "   能量: 模式 %d, 节流 %d%%\n"
            "   %s\n",
            g_soul ? soul_tick(g_soul) : 0,
            g_soul ? soul_drive(g_soul) : 0,
            g_soul ? soul_hw_stress(g_soul) : 0,
            g_soul ? soul_gen_count(g_soul) : 6,
            active_branches, exp_cnt,
            0, 0,  /* snapshot stats */
            1, 0   /* energy stats: mode=1, throttle=0 */,
            sb_sum);
    } else if (strcmp(buf, "exit") == 0 || strcmp(buf, "quit") == 0) {
        snprintf(response, sizeof(response), "bye\n");
        write(client_fd, response, strlen(response));
        close(client_fd);
        return;
    } else {
        /* Normal question */
        query_handle(buf, g_soul, response, sizeof(response));
        strcat(response, "\n");
    }
    
    write(client_fd, response, strlen(response));
    close(client_fd);
}

void torkd_tick(void) {
    if (!g_started || g_server_fd < 0) return;
    
    /* Accept all pending connections */
    while (1) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(g_server_fd, (struct sockaddr*)&client_addr, &client_len);
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
    
    char buf[TORKD_MAX_MSG];
    snprintf(buf, sizeof(buf), "%s\n", question);
    write(fd, buf, strlen(buf));
    
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
