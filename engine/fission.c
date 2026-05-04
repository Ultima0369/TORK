#include "fission.h"
#include "monitor.h"
#include "../learning/pi_seed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define FISSION_LOCK_PATH "/tmp/tork_fission.lock"
#define FISSION_TICK_THRESHOLD 5000
#define FISSION_CHILD_DIR_PREFIX "tork_fission_"

int fission_decide(const soul_t *soul) {
    if (soul_tick((soul_t *)soul) < FISSION_TICK_THRESHOLD)
        return 0;
    if (soul_hw_stress((soul_t *)soul) >= 2)
        return 0;
    if (soul_drive((soul_t *)soul) < 20)
        return 0;

    /* Check lock file */
    if (access(FISSION_LOCK_PATH, F_OK) == 0)
        return 0;

    static int pi_fission_inited = 0;
    if (!pi_fission_inited) { pi_seed_init(); pi_fission_inited = 1; }

    /* π-Seed random backoff: prevent thundering herd fission */
    float fission_roll = pi_seed_float();
    if (fission_roll < 0.4f) return 0;  /* 40% chance to defer fission */

    return 1;
}

static int create_lock(void) {
    int fd = open(FISSION_LOCK_PATH, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return -1;
    char pid_str[16];
    snprintf(pid_str, sizeof(pid_str), "%d\n", getpid());
    write(fd, pid_str, strlen(pid_str));
    close(fd);
    return 0;
}

void fission_cleanup(void) {
    unlink(FISSION_LOCK_PATH);
}

pid_t fission_spawn(void) {
    if (create_lock() != 0) return -1;

    pid_t child = fork();
    if (child < 0) {
        unlink(FISSION_LOCK_PATH);
        return -1;
    }

    if (child == 0) {
        /* Child process */
        prctl(PR_SET_NAME, "tork_child", 0, 0, 0);

        char workdir[128];
        snprintf(workdir, sizeof(workdir), "%s_%d", FISSION_CHILD_DIR_PREFIX, getpid());

        if (mkdir(workdir, 0755) != 0 && errno != EEXIST) _exit(1);

        /* Copy project structure */
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "cp -r build core engine instinct code benchmark %s/ 2>/dev/null", workdir);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "cp Makefile build.sh %s/ 2>/dev/null", workdir);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "cp -r benchmark/memcpy %s/benchmark/ 2>/dev/null", workdir);
        system(cmd);

        if (chdir(workdir) != 0) _exit(1);

        execl("./build/tork_engine", "tork_child", "500", NULL);
        _exit(1);
    }

    /* Parent: wait briefly for child to start */
    usleep(500000);
    return child;
}

int fission_collect(pid_t child_pid, int timeout_ticks) {
    /* Wait for child to accumulate some ticks */
    for (int i = 0; i < timeout_ticks; i++) {
        if (kill(child_pid, 0) != 0)
            return -1; /* child dead */
        usleep(50000); /* 50ms per check */
    }

    /* Try to read child soul */
    soul_t child_soul;
    if (soul_open(&child_soul, child_pid) != 0)
        return 1; /* can't read → parent wins by default */

    if (soul_read(&child_soul) != 0) {
        soul_close(&child_soul);
        return 1;
    }

    uint8_t child_opt = soul_code_opt_saved(&child_soul);
    uint8_t child_nop = soul_code_nop_count(&child_soul);
    soul_close(&child_soul);

    /* Simple heuristic: more optimizations = better */
    int child_score = (int)child_opt + (child_nop == 0 ? 1 : 0);

    return (child_score > 1) ? 0 : 1;
}

int fission_migrate(pid_t child_pid) {
    int result = fission_collect(child_pid, 20);

    if (result == 0) {
        /* Child is better — parent yields */
        printf("FISSION: child %d wins, parent yielding\n", child_pid);
        fission_cleanup();
        exit(0);
    }

    /* Parent is better or child is dead */
    printf("FISSION: parent wins, terminating child %d\n", child_pid);
    kill(child_pid, SIGTERM);
    int st;
    waitpid(child_pid, &st, 0);

    /* Clean up child directory */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s_%d", FISSION_CHILD_DIR_PREFIX, child_pid);
    system(cmd);

    fission_cleanup();
    return 0;
}