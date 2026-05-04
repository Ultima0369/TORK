/* 🥚 tork — 连接 TORK 守护进程并提问
 * 用法: ./build/tork "你怎么样？"
 *       ./build/tork status
 *       ./build/tork stop
 * 需要 torkd 正在运行。
 */

#include "../engine/torkd.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("🥚 TORK 客户端\n");
        printf("   用法: ./build/tork <问题>\n");
        printf("   示例: ./build/tork \"你怎么样？\"\n");
        printf("         ./build/tork status\n");
        printf("         ./build/tork stop\n");
        return 0;
    }
    
    if (!torkd_is_running()) {
        printf("❌ TORK 守护进程未运行\n");
        printf("   请先启动: ./build/torkd_start\n");
        return 1;
    }
    
    char response[4096];
    int ret = torkd_query(argv[1], response, sizeof(response));
    
    if (ret < 0) {
        printf("❌ 查询失败\n");
        return 1;
    }
    
    printf("\033[36m🥚 TORK:\n%s\033[0m", response);
    
    return 0;
}
