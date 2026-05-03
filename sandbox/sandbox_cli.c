/*
 * TORK Sandbox CLI — 供云端代理调用的沙箱命令行工具
 * 
 * 用法: tork_sandbox <command> <timeout_seconds>
 * 输出: JSON
 */

#include "sandbox.h"
#include "../install/agreement.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [timeout_sec]\n", argv[0]);
        return 1;
    }

    /* Check agreement first */
    agreement_state_t state = agreement_check();
    if (state != AGREE_ACCEPTED) {
        /* No agreement - output JSON error */
        printf("{\"exit_code\":401,\"stdout\":\"\","
               "\"stderr\":\"TORK: No agreement found. Run install/install.sh first.\","
               "\"timed_out\":false,\"elapsed_ms\":0}\n");
        return 1;
    }

    const char *command = argv[1];
    int timeout = 30;
    if (argc >= 3) {
        timeout = atoi(argv[2]);
        if (timeout < 1) timeout = 1;
        if (timeout > 300) timeout = 300;
    }

    sandbox_result_t result = sandbox_exec(command, timeout);
    
    /* Output JSON */
    printf("{\"exit_code\":%d,\"stdout\":\"", result.exit_code);
    
    /* Escape stdout for JSON */
    if (result.stdout_str) {
        for (char *p = result.stdout_str; *p; p++) {
            switch (*p) {
                case '"':  printf("\\\""); break;
                case '\\': printf("\\\\"); break;
                case '\n': printf("\\n"); break;
                case '\r': printf("\\r"); break;
                case '\t': printf("\\t"); break;
                default:   putchar(*p);
            }
        }
    }
    
    printf("\",\"stderr\":\"");
    
    /* Escape stderr for JSON */
    if (result.stderr_str) {
        for (char *p = result.stderr_str; *p; p++) {
            switch (*p) {
                case '"':  printf("\\\""); break;
                case '\\': printf("\\\\"); break;
                case '\n': printf("\\n"); break;
                case '\r': printf("\\r"); break;
                case '\t': printf("\\t"); break;
                default:   putchar(*p);
            }
        }
    }
    
    printf("\",\"timed_out\":%s,\"elapsed_ms\":%.1f}\n",
           result.timed_out ? "true" : "false",
           result.elapsed_ms);

    sandbox_free_result(&result);
    return result.exit_code == 0 ? 0 : 1;
}
