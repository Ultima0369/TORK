/* tork_ask — Ask TORK
 * 用法: ./build/tork_ask "你的问题?"
 * TORK 用自己的记忆回答, 不需要云端 API。
 */

#include "query.h"
#include "soul_access.h"
#include "../learning/watcher.h"
#include "../learning/snapshot.h"
#include "../learning/observer.h"
#include "../learning/energy.h"
#include "../learning/experience.h"
#include "../learning/branch.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Ask TORK:\n");
        printf("   ./build/tork_ask \"你怎么样?\"\n");
        printf("   ./build/tork_ask \"我的电脑什么配置?\"\n");
        printf("   ./build/tork_ask \"给我点建议\"\n");
        return 0;
    }
    
    /* Initialize subsystems */
    watcher_init();
    snap_init();
    obs_init();
    eng_init();
    exp_init();
    
    /* Try to load persistent state */
    snap_load();
    obs_load_baseline();
    watcher_load();
    
    /* Scan /proc for current state */
    watcher_scan_proc();
    
    /* Build a minimal soul from current state */
    soul_t soul;
    memset(&soul, 0, sizeof(soul));
    soul.mem_fd = -1;
    soul.wr_fd = -1;
    soul.pid = 0;
    /* Initialize soul data buffer */
    SOUL_U32_SET(&soul, S_TICK, 0);        /* tick starts at 0 */
    soul.buf[S_HW_STRESS] = 0;           /* stress = 0 (normal) */
    soul.buf[S_DRIVE] = 55;              /* drive = +55 (curious) */
    soul.buf[S_MODE] = 1;                /* mode = running */
    soul.buf[S_SANDBOX_LEVEL] = 3;       /* sandbox = level 3 */
    SOUL_U32_SET(&soul, S_GEN_COUNT, 6);    /* gen 6 */
    soul.buf[S_AGREED] = 1;              /* agreed */
    
    /* Get response */
    char response[QUERY_MAX_RESPONSE];
    query_handle(argv[1], &soul, response, sizeof(response));
    
    printf("\033[36m"); /* cyan */
    printf("TORK:\n%s\n", response);
    printf("\033[0m");
    
    return 0;
}
