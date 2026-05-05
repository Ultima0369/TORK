#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <linux/limits.h>
#include <linux/sched.h>

static volatile sig_atomic_t child_pid = 0;

static void sig_handler(int sig) {
    (void)sig;
    if (child_pid > 0) kill(child_pid, SIGTERM);
}

static int mkdir_p(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    char *p = NULL;
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int bind_mount_ro(const char *src, const char *dst) {
    if (mkdir_p(dst, 0755) != 0) {
        fprintf(stderr, "sandbox: mkdir %s: %s\n", dst, strerror(errno));
        return -1;
    }
    if (mount(src, dst, NULL, MS_BIND | MS_REC, NULL) != 0) {
        fprintf(stderr, "sandbox: bind %s→%s: %s\n", src, dst, strerror(errno));
        return -1;
    }
    if (mount(NULL, dst, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_REC, NULL) != 0) {
        fprintf(stderr, "sandbox: ro %s: %s\n", dst, strerror(errno));
        return -1;
    }
    return 0;
}

static int bind_mount_rw(const char *src, const char *dst) {
    if (mkdir_p(dst, 0755) != 0) {
        fprintf(stderr, "sandbox: mkdir %s: %s\n", dst, strerror(errno));
        return -1;
    }
    if (mount(src, dst, NULL, MS_BIND | MS_REC, NULL) != 0) {
        fprintf(stderr, "sandbox: bind %s→%s: %s\n", src, dst, strerror(errno));
        return -1;
    }
    return 0;
}

static int pivot_root(const char *new_root, const char *put_old) {
    return syscall(SYS_pivot_root, new_root, put_old);
}

static int setup_cgroup_v2(pid_t pid) {
    const char *base = "/sys/fs/cgroup";
    char path[PATH_MAX];
    int fd;

    snprintf(path, sizeof(path), "%s/tork", base);
    if (mkdir_p(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "sandbox: cgroup mkdir %s: %s\n", path, strerror(errno));
        return -1;
    }

    snprintf(path, sizeof(path), "%s/tork/cgroup.procs", base);
    fd = open(path, O_WRONLY | O_APPEND);
    if (fd < 0) {
        fprintf(stderr, "sandbox: cgroup.procs: %s\n", strerror(errno));
        return -1;
    }
    char pidbuf[32];
    int len = snprintf(pidbuf, sizeof(pidbuf), "%d", pid);
    write(fd, pidbuf, len);
    close(fd);

    snprintf(path, sizeof(path), "%s/tork/pids.max", base);
    fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, "64\n", 3); close(fd); }

    snprintf(path, sizeof(path), "%s/tork/memory.max", base);
    fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, "536870912\n", 10); close(fd); }

    snprintf(path, sizeof(path), "%s/tork/cpu.max", base);
    fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, "50000 100000\n", 13); close(fd); }

    return 0;
}

static int sandbox_child(void *arg) {
    const char *project_dir = (const char *)arg;
    char src[PATH_MAX], dst[PATH_MAX];
    const char *rootfs = "/tmp/tork_rootfs";

    mkdir_p(rootfs, 0755);
    mkdir_p("/tmp/tork_rootfs/old_root", 0755);
    mkdir_p("/tmp/tork_rootfs/build", 0755);
    mkdir_p("/tmp/tork_rootfs/persist", 0755);
    mkdir_p("/tmp/tork_rootfs/proc", 0755);
    mkdir_p("/tmp/tork_rootfs/sys/class/thermal/thermal_zone0", 0755);
    mkdir_p("/tmp/tork_rootfs/sys/devices/virtual/thermal/thermal_zone0", 0755);
    mkdir_p("/tmp/tork_rootfs/sys/devices/system/cpu/cpu0/cpufreq", 0755);
    mkdir_p("/tmp/tork_rootfs/dev", 0755);
    mkdir_p("/tmp/tork_rootfs/dev/shm", 0755);
    mkdir_p("/tmp/tork_rootfs/tmp", 0755);
    mkdir_p("/tmp/tork_rootfs/etc/tork", 0755);
    mkdir_p("/tmp/tork_rootfs/lib64", 0755);
    mkdir_p("/tmp/tork_rootfs/lib/x86_64-linux-gnu", 0755);

    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
        fprintf(stderr, "sandbox: MS_PRIVATE: %s\n", strerror(errno));
        return 1;
    }

    if (mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL) != 0) {
        fprintf(stderr, "sandbox: self-bind %s: %s\n", rootfs, strerror(errno));
        return 1;
    }

    snprintf(src, sizeof(src), "%s/build", project_dir);
    snprintf(dst, sizeof(dst), "%s/build", rootfs);
    if (bind_mount_ro(src, dst) != 0) return 1;

    snprintf(src, sizeof(src), "%s/persist", project_dir);
    snprintf(dst, sizeof(dst), "%s/persist", rootfs);
    if (bind_mount_rw(src, dst) != 0) return 1;

    snprintf(dst, sizeof(dst), "%s/sys/devices/virtual/thermal/thermal_zone0", rootfs);
    if (access("/sys/devices/virtual/thermal/thermal_zone0/temp", F_OK) == 0) {
        if (bind_mount_ro("/sys/devices/virtual/thermal/thermal_zone0", dst) != 0) {
            fprintf(stderr, "sandbox: thermal zone not available (non-fatal)\n");
        } else {
            snprintf(dst, sizeof(dst), "%s/sys/class/thermal/thermal_zone0", rootfs);
            mkdir_p(dst, 0755);
            if (mount("/sys/devices/virtual/thermal/thermal_zone0", dst,
                      NULL, MS_BIND, NULL) != 0) {
                fprintf(stderr, "sandbox: thermal symlink path: %s (non-fatal)\n", strerror(errno));
            } else {
                mount(NULL, dst, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY, NULL);
            }
        }
    }

    if (access("/sys/devices/system/cpu/cpu0/cpufreq", F_OK) == 0) {
        snprintf(dst, sizeof(dst), "%s/sys/devices/system/cpu/cpu0/cpufreq", rootfs);
        if (bind_mount_ro("/sys/devices/system/cpu/cpu0/cpufreq", dst) != 0) {
            fprintf(stderr, "sandbox: cpufreq not available (non-fatal)\n");
        }
    }

    if (access("/etc/tork/agreement.sig", F_OK) == 0) {
        snprintf(dst, sizeof(dst), "%s/etc/tork", rootfs);
        if (bind_mount_ro("/etc/tork", dst) != 0) {
            fprintf(stderr, "sandbox: /etc/tork not available (non-fatal)\n");
        }
    }

    snprintf(dst, sizeof(dst), "%s/lib64", rootfs);
    if (bind_mount_ro("/lib64", dst) != 0) {
        fprintf(stderr, "sandbox: /lib64 not available (non-fatal)\n");
    }

    if (access("/lib/x86_64-linux-gnu", F_OK) == 0) {
        snprintf(dst, sizeof(dst), "%s/lib/x86_64-linux-gnu", rootfs);
        if (bind_mount_ro("/lib/x86_64-linux-gnu", dst) != 0) {
            fprintf(stderr, "sandbox: /lib/x86_64-linux-gnu not available (non-fatal)\n");
        }
    }

    if (pivot_root(rootfs, "/tmp/tork_rootfs/old_root") != 0) {
        fprintf(stderr, "sandbox: pivot_root: %s\n", strerror(errno));
        return 1;
    }

    if (chdir("/") != 0) {
        fprintf(stderr, "sandbox: chdir: %s\n", strerror(errno));
        return 1;
    }

    if (umount2("/old_root", MNT_DETACH) != 0) {
        fprintf(stderr, "sandbox: umount old_root: %s\n", strerror(errno));
    }

    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) != 0) {
        fprintf(stderr, "sandbox: mount /proc: %s\n", strerror(errno));
        return 1;
    }

    if (mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV,
              "size=64m,mode=1777") != 0) {
        fprintf(stderr, "sandbox: mount /tmp: %s\n", strerror(errno));
        return 1;
    }

    mount("tmpfs", "/dev", "tmpfs", MS_NOSUID | MS_NOEXEC,
          "size=4m,mode=0755");
    mkdir_p("/dev/shm", 0755);
    mount("tmpfs", "/dev/shm", "tmpfs", MS_NOSUID | MS_NODEV,
          "size=32m,mode=1777");

    mkdir_p("/dev/null", 0755);
    {
        int fd = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd >= 0) close(fd);
    }

    fprintf(stderr, "TORK sandbox initialized\n");
    fflush(stderr);

    /* ── 隔离性自检 ── */
    {
        int pass = 0, fail = 0;

        /* 温度读取 */
        int fd = open("/sys/class/thermal/thermal_zone0/temp", O_RDONLY);
        if (fd >= 0) {
            char buf[32] = {0};
            read(fd, buf, sizeof(buf) - 1);
            close(fd);
            int temp = atoi(buf);
            if (temp > 0) { fprintf(stderr, "  CHECK: thermal=%d°C ✓\n", temp / 1000); pass++; }
            else { fprintf(stderr, "  CHECK: thermal=invalid ✗\n"); fail++; }
        } else {
            fprintf(stderr, "  CHECK: thermal not available ✗\n"); fail++;
        }

        /* /home 不可访问 */
        if (access("/home", F_OK) != 0) { fprintf(stderr, "  CHECK: /home isolated ✓\n"); pass++; }
        else { fprintf(stderr, "  CHECK: /home visible ✗\n"); fail++; }

        /* /proc/net 隔离 */
        fd = open("/proc/net/dev", O_RDONLY);
        if (fd >= 0) {
            char buf[256] = {0};
            read(fd, buf, sizeof(buf) - 1);
            close(fd);
            int lines = 0;
            for (char *p = buf; *p; p++) if (*p == '\n') lines++;
            if (lines <= 2) { fprintf(stderr, "  CHECK: net isolated (only lo) ✓\n"); pass++; }
            else { fprintf(stderr, "  CHECK: net has interfaces ✗\n"); fail++; }
        }

        fprintf(stderr, "  SANDBOX CHECKS: %d/%d passed\n", pass, pass + fail);
    }

    char *args[] = { "tork_engine", "--daemon", NULL };
    execv("/build/tork_engine", args);

    fprintf(stderr, "sandbox: exec /build/tork_engine: %s\n", strerror(errno));
    return 1;
}

#define STACK_SIZE (1024 * 1024)

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    char project_dir[PATH_MAX];
    if (realpath(".", project_dir) == NULL) {
        perror("realpath");
        return 1;
    }

    void *stack = malloc(STACK_SIZE);
    if (!stack) {
        perror("malloc");
        return 1;
    }

    int clone_flags = CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWIPC;
    if (geteuid() != 0) {
        fprintf(stderr, "sandbox: must run as root (ptrace needs same user namespace)\n");
        free(stack);
        return 1;
    }

    pid_t pid = clone(sandbox_child, stack + STACK_SIZE,
                       SIGCHLD | clone_flags, project_dir);
    if (pid < 0) {
        fprintf(stderr, "sandbox: clone: %s\n", strerror(errno));
        free(stack);
        return 1;
    }

    child_pid = pid;
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    setup_cgroup_v2(pid);

    fprintf(stderr, "TORK sandbox started (PID %d)\n", pid);

    int status;
    waitpid(pid, &status, 0);

    free(stack);

    if (WIFEXITED(status)) {
        fprintf(stderr, "TORK sandbox exited (code %d)\n", WEXITSTATUS(status));
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "TORK sandbox killed (signal %d)\n", WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    return 1;
}