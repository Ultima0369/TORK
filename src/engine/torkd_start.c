/* torkd_start — 启动独立的 TORK 守护进程
 * 用法: ./build/torkd_start
 * TORK 将在后台监听 /tmp/torkd.sock
 * 注: tork_engine 已自带 socket 服务, 此文件仅用于独立运行
 */

#include "torkd.h"
#include "query.h"
#include "../learning/watcher.h"
#include "../learning/snapshot.h"
#include "../learning/self_build.h"
#include "../learning/mutation_guide.h"
#include "../learning/experience.h"
#include "../learning/branch.h"
#include "../learning/pattern.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

static int g_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    
    /* Fork to background */
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid > 0) {
        printf("TORK daemon started (PID %d, socket %s)\n", pid, TORKD_SOCKET_PATH);
        return 0;
    }
    
    /* Child: detach */
    setsid();
    /* Close stdio */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    int fd = open("/tmp/torkd.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) { dup2(fd, STDERR_FILENO); close(fd); }
    
    /* Init subsystems */
    watcher_init();
    snap_init();
    exp_init();
    br_init();
    pat_init();
    sb_init();
    mg_init();
    watcher_load();
    snap_load();
    sb_load();
    mg_load();

    /* 不初始化 task 队列 — torkd_start 是独立守护进程，
     * task 命令会返回提示让用户通过引擎进程执行 */
    printf("  TORKD: standalone daemon started (task execution requires tork_engine)\n");

    /* Init socket (without a real soul — query handles this) */
    torkd_init(NULL);
    
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);
    
    /* Simple serve loop */
    uint32_t tick = 0;
    while (g_running) {
        torkd_tick();
        tick++;
        if (tick % 10 == 0) watcher_scan_proc();
        if (tick % 100 == 0) {
            watcher_learn_patterns();
            sb_check_sources();
        }
        if (tick % 1000 == 0) {
            watcher_save();
            sb_save();
            mg_save();
        }
        usleep(100000);  /* 100ms */
    }
    
    torkd_shutdown();
    watcher_save();
    sb_save();
    mg_save();
    return 0;
}
