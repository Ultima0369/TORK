/* 🥚 torkd_start — 启动 TORK 守护进程
 * 用法: ./build/torkd_start
 * TORK 将在后台监听 /tmp/torkd.sock
 */

#include "torkd.h"
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (torkd_is_running()) {
        printf("🥚 TORK daemon already running (socket %s)\n", TORKD_SOCKET_PATH);
        printf("   Try: ./build/tork \"你好？\"\n");
        return 0;
    }
    
    printf("🥚 Starting TORK daemon...\n");
    if (torkd_start() == 0) {
        printf("✅ TORK daemon started\n");
        printf("   Socket: %s\n", TORKD_SOCKET_PATH);
        printf("   Try: ./build/tork \"你怎么样？\"\n");
        return 0;
    } else {
        printf("❌ Failed to start TORK daemon\n");
        return 1;
    }
}
