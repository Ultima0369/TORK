#include "torkd.h"
#include "query.h"
#include "soul_access.h"
#include "../learning/watcher.h"
#include "../learning/snapshot.h"
#include "../learning/observer.h"
#include "../learning/energy.h"
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
static pid_t g_server_pid = 0;

/* ── 处理客户端查询 ────────────────────────────────────────── */
static void handle_client(int client_fd, soul_t *soul) {
    char buf[TORKD_MAX_MSG];
    memset(buf, 0, sizeof(buf));
    
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    
    /* Trim newline */
    if (buf[n-1] == '\n') buf[n-1] = '\0';
    
    /* Update watcher state */
    watcher_scan_proc();
    
    /* Handle special commands */
    char response[TORKD_MAX_MSG];
    
    if (strcmp(buf, "ping") == 0) {
        snprintf(response, sizeof(response), "pong\n");
    } else if (strcmp(buf, "status") == 0) {
        char summary[512];
        sb_summary(summary, sizeof(summary));
        char mg_sum[512];
        mg_summary(mg_sum, sizeof(mg_sum));
        
        snprintf(response, sizeof(response),
            "🥚 TORK v3.3 守护进程\n"
            "   心跳: %u tick | 驱动: %+d\n"
            "   压力: %u | 世代: %u\n"
            "   分支: %d 活跃 | 模式: %d 条\n"
            "   经验: %u 条\n"
            "   %s\n"
            "   %s\n",
            soul_tick(soul), soul_drive(soul),
            soul_hw_stress(soul), soul_gen_count(soul),
            br_active_count(), 0, /* pattern count */
            (unsigned)0, /* exp count via API */
            summary, mg_sum);
    } else if (strcmp(buf, "quit") == 0 || strcmp(buf, "stop") == 0) {
        snprintf(response, sizeof(response), "bye\n");
        write(client_fd, response, strlen(response));
        close(client_fd);
        torkd_stop();
        return;
    } else {
        /* Normal question */
        query_handle(buf, soul, response, sizeof(response));
        strcat(response, "\n");
    }
    
    write(client_fd, response, strlen(response));
    close(client_fd);
}

/* ── 服务主循环 ────────────────────────────────────────────── */
void torkd_serve(void) {
    /* Build a soul from accumulated state */
    soul_t soul;
    memset(&soul, 0, sizeof(soul));
    soul.mem_fd = -1;
    soul.wr_fd = -1;
    soul.pid = getpid();
    SOUL_U32(&soul, S_TICK) = 0;
    soul.buf[S_HW_STRESS] = 0;
    soul.buf[S_DRIVE] = 55;
    soul.buf[S_MODE] = 1;
    soul.buf[S_SANDBOX_LEVEL] = 3;
    SOUL_U32(&soul, S_GEN_COUNT) = 6;
    soul.buf[S_AGREED] = 1;
    
    /* Remove old socket file */
    unlink(TORKD_SOCKET_PATH);
    
    /* Create socket */
    g_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        perror("torkd socket");
        _exit(1);
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, TORKD_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (bind(g_server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("torkd bind");
        close(g_server_fd);
        _exit(1);
    }
    
    /* Set permissions so any user can connect */
    chmod(TORKD_SOCKET_PATH, 0666);
    
    if (listen(g_server_fd, TORKD_BACKLOG) < 0) {
        perror("torkd listen");
        close(g_server_fd);
        _exit(1);
    }
    
    printf("  TORKD: listening on %s (PID %d)\n", TORKD_SOCKET_PATH, getpid());
    
    /* Signal handler for graceful shutdown */
    signal(SIGPIPE, SIG_IGN);
    
    /* Accept loop */
    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    /* Update tick in a basic loop */
    uint32_t tick = 0;
    
    while (1) {
        /* Set socket non-blocking with select() for timeout */
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(g_server_fd, &read_fds);
        
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000;  /* 500ms timeout */
        
        int ret = select(g_server_fd + 1, &read_fds, NULL, NULL, &tv);
        
        if (ret > 0 && FD_ISSET(g_server_fd, &read_fds)) {
            int client_fd = accept(g_server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd >= 0) {
                handle_client(client_fd, &soul);
            }
        }
        
        /* Update tick */
        tick++;
        if (tick % 10 == 0) {
            SOUL_U32(&soul, S_TICK) = tick;
            watcher_scan_proc();
            sb_check_sources();
        }
        
        /* Every 100 ticks → learn patterns */
        if (tick % 100 == 0) {
            watcher_learn_patterns();
            /* Save state periodically */
            if (tick % 1000 == 0) {
                watcher_save();
                sb_save();
                mg_save();
            }
        }
    }
    
    close(g_server_fd);
    unlink(TORKD_SOCKET_PATH);
}

/* ── 启动守护进程 ────────────────────────────────────────── */
int torkd_start(void) {
    if (torkd_is_running()) {
        printf("  TORKD: already running (PID %d)\n", g_server_pid);
        return 0;
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        perror("torkd fork");
        return -1;
    }
    
    if (pid == 0) {
        /* Child: detach and serve */
        setsid();
        /* Close stdin/stdout, keep stderr for logs */
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        /* Redirect to /dev/null or logfile */
        int fd = open("/tmp/torkd.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        
        /* Initialize subsystems */
        watcher_init();
        snap_init();
        obs_init();
        eng_init();
        exp_init();
        sb_init();
        mg_init();
        
        /* Load persistent state */
        watcher_load();
        snap_load();
        obs_load_baseline();
        sb_load();
        mg_load();
        
        /* Start serving */
        torkd_serve();
        _exit(0);
    }
    
    /* Parent */
    g_server_pid = pid;
    printf("  TORKD: started (PID %d, socket %s)\n", pid, TORKD_SOCKET_PATH);
    
    /* Wait for socket to be ready */
    usleep(200000);
    
    return 0;
}

/* ── 停止 ────────────────────────────────────────────────── */
void torkd_stop(void) {
    if (g_server_pid > 0) {
        kill(g_server_pid, SIGTERM);
        printf("  TORKD: stopped (PID %d)\n", g_server_pid);
        g_server_pid = 0;
    }
    unlink(TORKD_SOCKET_PATH);
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
}

/* ── 查询 ────────────────────────────────────────────────── */
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
    
    /* Send question */
    char buf[TORKD_MAX_MSG];
    snprintf(buf, sizeof(buf), "%s\n", question);
    write(fd, buf, strlen(buf));
    
    /* Read response */
    memset(response, 0, max_len);
    ssize_t n = read(fd, response, max_len - 1);
    if (n < 0) n = 0;
    response[n] = '\0';
    
    close(fd);
    return 0;
}

/* ── 检查运行状态 ────────────────────────────────────────── */
int torkd_is_running(void) {
    if (g_server_pid > 0 && kill(g_server_pid, 0) == 0) {
        return 1;
    }
    /* Also check socket */
    struct stat st;
    if (stat(TORKD_SOCKET_PATH, &st) == 0) {
        /* Try to connect */
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return 0;
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, TORKD_SOCKET_PATH, sizeof(addr.sun_path) - 1);
        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            close(fd);
            return 1;
        }
        close(fd);
    }
    g_server_pid = 0;
    return 0;
}
