#include "sandbox.h"
#include "../install/agreement.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

/* ── 命令分类表 ──────────────────────────────────────────────── */

/* 安全命令白名单 */
static const char *safe_cmds[] = {
    "ls", "cat", "head", "tail", "less", "more",
    "find", "grep", "egrep", "fgrep", "ack",
    "wc", "sort", "uniq", "cut", "tr", "tee",
    "echo", "printf", "which", "whereis",
    "file", "stat", "du", "df", "free", "uptime",
    "ps", "top", "htop", "lsof",
    "date", "cal", "nproc", "uname", "arch",
    "env", "printenv",
    "pwd", "realpath", "readlink",
    "who", "whoami", "id", "groups",
    "dmesg", "lscpu", "lsblk", "lspci", "lsusb",
    "ip", "ss", "netstat", "ping", "traceroute",
    "dig", "nslookup", "host",
    "git", "diff", "cmp", "md5sum", "sha256sum",
    "xxd", "hexdump", "od",
    NULL
};

/* 写入命令 */
static const char *write_cmds[] = {
    "cp", "mv", "mkdir", "rmdir", "touch",
    "chmod", "chown", "ln",
    "tar", "gzip", "gunzip", "bzip2", "xz",
    "zip", "unzip",
    "install",
    NULL
};

/* 执行命令 */
static const char *exec_cmds[] = {
    "gcc", "g++", "clang", "make", "cmake",
    "python3", "python", "node", "deno", "go", "rustc",
    "as", "ld", "ar", "nm", "objdump", "strip",
    "perl", "ruby", "lua", "php",
    "sh", "bash", "zsh", "fish",
    NULL
};

/* 网络命令 */
static const char *net_cmds[] = {
    "curl", "wget", "nc", "socat",
    "ssh", "scp", "rsync",
    "git clone", "git fetch", "git pull",
    NULL
};

/* 系统命令 */
static const char *sys_cmds[] = {
    "apt", "apt-get", "dpkg", "snap", "flatpak",
    "systemctl", "service", "journalctl",
    "sysctl", "modprobe",
    "mount", "umount",
    "ufw", "iptables",
    "crontab",
    NULL
};

/* 危险命令关键词 */
static const char *dangerous_patterns[] = {
    "rm -rf /", "rm -rf /*", "rm -rf ~",
    "dd if=", "mkfs", "fdisk", "parted",
    "chmod -R 0", "chown -R",
    "> /dev/sd", "> /dev/nvme",
    "wget -O- | sh", "curl -sL | bash",
    "passwd", "sudo",
    NULL
};

/* ── 命令分类 ─────────────────────────────────────────────────── */
static int str_startswith(const char *str, const char *prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

cmd_category_t sandbox_classify(const char *command) {
    if (!command) return CMD_UNKNOWN;

    /* Skip leading whitespace */
    while (*command == ' ' || *command == '\t') command++;
    if (*command == '\0') return CMD_UNKNOWN;

    /* Extract base command (first word) */
    char base[128];
    int i = 0;
    while (*command && *command != ' ' && *command != '\t' && i < 127) {
        base[i++] = *command++;
    }
    base[i] = '\0';

    /* Check dangerous patterns first */
    for (int d = 0; dangerous_patterns[d]; d++) {
        if (strstr(command, dangerous_patterns[d]))
            return CMD_DANGEROUS;
    }

    /* Check by category */
    for (int s = 0; safe_cmds[s]; s++) {
        if (strcmp(base, safe_cmds[s]) == 0) return CMD_READ;
    }
    for (int w = 0; write_cmds[w]; w++) {
        if (strcmp(base, write_cmds[w]) == 0) return CMD_WRITE;
    }
    for (int e = 0; exec_cmds[e]; e++) {
        if (strcmp(base, exec_cmds[e]) == 0) return CMD_EXEC;
    }
    for (int n = 0; net_cmds[n]; n++) {
        if (str_startswith(command, net_cmds[n])) return CMD_NET;
    }
    for (int s = 0; sys_cmds[s]; s++) {
        if (strcmp(base, sys_cmds[s]) == 0) return CMD_SYS;
    }

    return CMD_UNKNOWN;
}

/* ── 权限检查 ─────────────────────────────────────────────────── */
int sandbox_allowed(const char *command, cmd_category_t cat) {
    /* Check what sandbox level we have */
    sandbox_level_t level;
    
    /* Read from agreement */
    int fd = open(AGREEMENT_PATH, O_RDONLY);
    if (fd < 0) return 0;
    tork_agreement_t ag;
    ssize_t n = read(fd, &ag, sizeof(ag));
    close(fd);
    if (n != sizeof(ag)) return 0;
    if (ag.state != AGREE_ACCEPTED) return 0;
    level = ag.sandbox;

    /* Permission matrix */
    switch (cat) {
        case CMD_READ:
            return (level >= SANDBOX_READ) ? 1 : 0;
        case CMD_WRITE:
            return (level >= SANDBOX_NORMAL) ? 1 : 0;
        case CMD_EXEC:
            return (level >= SANDBOX_NORMAL) ? 1 : 0;
        case CMD_NET:
            return (level >= SANDBOX_NORMAL) ? 1 : 0;
        case CMD_SYS:
            return (level >= SANDBOX_FULL) ? 1 : 0;
        case CMD_DANGEROUS:
            return 0; /* Never allow */
        case CMD_UNKNOWN:
            return (level >= SANDBOX_FULL) ? 1 : 0;
    }
    return 0;
}

/* ── 执行（带超时和输出捕获） ────────────────────────────────── */
sandbox_result_t sandbox_exec(const char *command, int timeout_sec) {
    sandbox_result_t result = {0};
    result.exit_code = -1;
    result.stdout_str = NULL;
    result.stderr_str = NULL;

    if (!command) return result;

    /* Classify and check */
    cmd_category_t cat = sandbox_classify(command);
    if (!sandbox_allowed(command, cat)) {
        result.exit_code = 403;
        result.stderr_str = strdup("TORK Sandbox: command not authorized at current permission level.");
        return result;
    }

    /* Create pipes for stdout and stderr */
    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0)
        return result;

    struct timeval start, end;
    gettimeofday(&start, NULL);

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return result;
    }

    if (pid == 0) {
        /* Child */
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        /* Execute via /bin/sh -c */
        execl("/bin/sh", "sh", "-c", command, NULL);
        _exit(255);
    }

    /* Parent */
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    /* Set alarm for timeout */
    int timed_out = 0;
    if (timeout_sec > 0) {
        alarm(0); /* Cancel any pending alarm */
        signal(SIGALRM, SIG_DFL); /* Will be handled by waitpid timeout below */
    }

    /* Read output in non-blocking way */
    /* We'll use a simpler approach: wait with timeout using poll/select */
    int stdout_fd = stdout_pipe[0];
    int stderr_fd = stderr_pipe[0];

    /* Set non-blocking */
    fcntl(stdout_fd, F_SETFL, fcntl(stdout_fd, F_GETFL) | O_NONBLOCK);
    fcntl(stderr_fd, F_SETFL, fcntl(stderr_fd, F_GETFL) | O_NONBLOCK);

    /* Read all output */
    char stdout_buf[65536] = {0};
    char stderr_buf[65536] = {0};
    int stdout_pos = 0, stderr_pos = 0;
    
    /* Poll loop */
    int max_fd = (stdout_fd > stderr_fd) ? stdout_fd : stderr_fd;
    int remaining = timeout_sec * 10; /* 100ms intervals */
    
    while (remaining > 0) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(stdout_fd, &rfds);
        FD_SET(stderr_fd, &rfds);
        
        struct timeval tv = {0, 100000}; /* 100ms */
        int ret = select(max_fd + 1, &rfds, NULL, NULL, &tv);
        
        if (ret > 0) {
            if (FD_ISSET(stdout_fd, &rfds)) {
                int n = read(stdout_fd, stdout_buf + stdout_pos, 
                            sizeof(stdout_buf) - stdout_pos - 1);
                if (n > 0) stdout_pos += n;
            }
            if (FD_ISSET(stderr_fd, &rfds)) {
                int n = read(stderr_fd, stderr_buf + stderr_pos,
                            sizeof(stderr_buf) - stderr_pos - 1);
                if (n > 0) stderr_pos += n;
            }
        }
        
        /* Check if child still alive */
        int status;
        pid_t wp = waitpid(pid, &status, WNOHANG);
        if (wp == pid) {
            result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            if (WIFSIGNALED(status))
                result.exit_code = -WTERMSIG(status);
            break;
        }
        
        remaining--;
    }
    
    if (remaining <= 0) {
        /* Timeout - kill child */
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        timed_out = 1;
        result.exit_code = -1;
    }

    /* Read any remaining output */
    char tmp[4096];
    int n;
    while ((n = read(stdout_fd, tmp, sizeof(tmp))) > 0) {
        if (stdout_pos + n < (int)sizeof(stdout_buf) - 1) {
            memcpy(stdout_buf + stdout_pos, tmp, n);
            stdout_pos += n;
        }
    }
    while ((n = read(stderr_fd, tmp, sizeof(tmp))) > 0) {
        if (stderr_pos + n < (int)sizeof(stderr_buf) - 1) {
            memcpy(stderr_buf + stderr_pos, tmp, n);
            stderr_pos += n;
        }
    }

    close(stdout_fd);
    close(stderr_fd);

    stdout_buf[stdout_pos] = '\0';
    stderr_buf[stderr_pos] = '\0';

    gettimeofday(&end, NULL);
    double elapsed = (end.tv_sec - start.tv_sec) * 1000.0 +
                     (end.tv_usec - start.tv_usec) / 1000.0;

    result.stdout_str = strdup(stdout_buf);
    result.stderr_str = strdup(stderr_buf);
    result.timed_out = timed_out;
    result.elapsed_ms = elapsed;

    return result;
}

/* ── JSON 格式执行 ────────────────────────────────────────────── */
char *sandbox_exec_json(const char *command, int timeout_sec) {
    sandbox_result_t r = sandbox_exec(command, timeout_sec);
    
    /* Escape strings for JSON */
    /* Simple escaping - replace " with \" and newlines with \\n */
    char *stdout_esc = NULL;
    char *stderr_esc = NULL;
    
    if (r.stdout_str) {
        size_t len = strlen(r.stdout_str);
        stdout_esc = malloc(len * 2 + 1);
        int j = 0;
        for (size_t i = 0; i < len; i++) {
            if (r.stdout_str[i] == '"') { stdout_esc[j++] = '\\'; stdout_esc[j++] = '"'; }
            else if (r.stdout_str[i] == '\n') { stdout_esc[j++] = '\\'; stdout_esc[j++] = 'n'; }
            else if (r.stdout_str[i] == '\\') { stdout_esc[j++] = '\\'; stdout_esc[j++] = '\\'; }
            else stdout_esc[j++] = r.stdout_str[i];
        }
        stdout_esc[j] = '\0';
    }
    
    if (r.stderr_str) {
        size_t len = strlen(r.stderr_str);
        stderr_esc = malloc(len * 2 + 1);
        int j = 0;
        for (size_t i = 0; i < len; i++) {
            if (r.stderr_str[i] == '"') { stderr_esc[j++] = '\\'; stderr_esc[j++] = '"'; }
            else if (r.stderr_str[i] == '\n') { stderr_esc[j++] = '\\'; stderr_esc[j++] = 'n'; }
            else if (r.stderr_str[i] == '\\') { stderr_esc[j++] = '\\'; stderr_esc[j++] = '\\'; }
            else stderr_esc[j++] = r.stderr_str[i];
        }
        stderr_esc[j] = '\0';
    }
    
    char *json = NULL;
    int ret = asprintf(&json,
        "{\"exit_code\":%d,\"stdout\":\"%s\",\"stderr\":\"%s\","
        "\"timed_out\":%s,\"elapsed_ms\":%.1f}",
        r.exit_code,
        stdout_esc ? stdout_esc : "",
        stderr_esc ? stderr_esc : "",
        r.timed_out ? "true" : "false",
        r.elapsed_ms);
    
    free(stdout_esc);
    free(stderr_esc);
    sandbox_free_result(&r);
    
    return json;
}

void sandbox_free_result(sandbox_result_t *r) {
    if (r->stdout_str) free(r->stdout_str);
    if (r->stderr_str) free(r->stderr_str);
    r->stdout_str = NULL;
    r->stderr_str = NULL;
}
