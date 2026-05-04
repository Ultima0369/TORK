#include "sandbox.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: tork_sandbox <command> [timeout_sec]\n");
        return 1;
    }

    int timeout = (argc >= 3) ? atoi(argv[2]) : 30;

    printf("TORK Sandbox — executing: %s (timeout: %ds)\n", argv[1], timeout);

    sandbox_result_t r = sandbox_exec(argv[1], timeout);

    printf("--- exit_code: %d ---\n", r.exit_code);
    if (r.stdout_buf[0])
        printf("--- stdout ---\n%s\n", r.stdout_buf);
    if (r.stderr_buf[0])
        printf("--- stderr ---\n%s\n", r.stderr_buf);
    if (r.timed_out)
        printf("--- TIMED OUT ---\n");

    cmd_category_t cat = sandbox_classify(argv[1]);
    const char *cat_names[] = {"UNKNOWN","READ","WRITE","EXEC","NET","SYS","DANGEROUS"};
    printf("--- category: %s ---\n", (cat >= 0 && cat <= 6) ? cat_names[cat] : "???");

    return r.exit_code;
}
