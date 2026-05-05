#include "sandbox.h"
#include "../install/agreement.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

/* ── 命令分类表 ──────────────────────────────────────────────── */

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
    "sleep", "true", "false", "yes", "seq", "test",
    NULL
};

static const char *write_cmds[] = {
    "cp", "mv", "mkdir", "rmdir", "touch",
    "chmod", "chown", "ln",
    "tar", "gzip", "gunzip", "bzip2", "xz",
    "zip", "unzip",
    "install",
    NULL
};

static const char *exec_cmds[] = {
    "gcc", "g++", "clang", "make", "cmake",
    "python3", "python", "node", "deno", "go", "rustc",
    "as", "ld", "ar", "nm", "objdump", "strip",
    "perl", "ruby", "lua", "php",
    "sh", "bash", "zsh", "fish",
    NULL
};

static const char *net_cmds[] = {
    "curl", "wget", "nc", "socat",
    "ssh", "scp", "rsync",
    "git clone", "git fetch", "git pull",
    NULL
};

static const char *sys_cmds[] = {
    "apt", "apt-get", "dpkg", "snap", "flatpak",
    "systemctl", "service", "journalctl",
    "sysctl", "modprobe",
    "mount", "umount",
    "ufw", "iptables",
    "crontab", "passwd",
    NULL
};

static const char *dangerous_patterns[] = {
    "rm -rf /", "rm -rf /*", "rm -rf ~",
    "dd if=", "mkfs", "fdisk", "parted",
    "chmod -R 0", "chown -R",
    "> /dev/sd", "> /dev/nvme",
    "wget -O- | sh", "curl -sL | bash",
    "sudo",
    NULL
};

/* ── 命令分类 ─────────────────────────────────────────────────── */
static int str_startswith(const char *str, const char *prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

cmd_category_t sandbox_classify(const char *command) {
    if (!command) return CMD_UNKNOWN;
    while (*command == ' ' || *command == '\t') command++;
    if (*command == '\0') return CMD_UNKNOWN;

    char base[128];
    int i = 0;
    const char *orig = command;
    while (*command && *command != ' ' && *command != '\t' && i < 127)
        base[i++] = *command++;
    base[i] = '\0';

    /* 先检查 base 命令分类，再检查危险模式（避免误判：cat passwd 应该是 READ 不是 DANGEROUS） */
    cmd_category_t base_cat = CMD_UNKNOWN;
    for (int s = 0; safe_cmds[s]; s++)
        if (strcmp(base, safe_cmds[s]) == 0) { base_cat = CMD_READ; break; }
    for (int w = 0; write_cmds[w]; w++)
        if (strcmp(base, write_cmds[w]) == 0) { base_cat = CMD_WRITE; break; }
    for (int e = 0; exec_cmds[e]; e++)
        if (strcmp(base, exec_cmds[e]) == 0) { base_cat = CMD_EXEC; break; }
    for (int n = 0; net_cmds[n]; n++)
        if (str_startswith(orig, net_cmds[n])) { base_cat = CMD_NET; break; }
    for (int s = 0; sys_cmds[s]; s++)
        if (strcmp(base, sys_cmds[s]) == 0) { base_cat = CMD_SYS; break; }

    /* DANGEROUS override: 任何命令含危险模式则覆盖 */
    for (int d = 0; dangerous_patterns[d]; d++)
        if (strstr(orig, dangerous_patterns[d]))
            return CMD_DANGEROUS;

    return base_cat;
}

/* ── 权限检查 ─────────────────────────────────────────────────── */
int sandbox_allowed(const char *command) {
    cmd_category_t cat = sandbox_classify(command);
    sandbox_level_t level;

    int fd = open(AGREEMENT_PATH, O_RDONLY);
    if (fd < 0) return 0;
    tork_agreement_t ag;
    ssize_t n = read(fd, &ag, sizeof(ag));
    close(fd);
    if (n != sizeof(ag)) return 0;
    if (ag.state != AGREE_ACCEPTED) return 0;
    level = ag.sandbox;

    switch (cat) {
    case CMD_READ:      return (level >= SANDBOX_READ);
    case CMD_WRITE:     return (level >= SANDBOX_NORMAL);
    case CMD_EXEC:      return (level >= SANDBOX_NORMAL);
    case CMD_NET:       return (level >= SANDBOX_NORMAL);
    case CMD_SYS:       return (level >= SANDBOX_FULL);
    case CMD_DANGEROUS: return 0;
    case CMD_UNKNOWN:   return (level >= SANDBOX_FULL);
    }
    return 0;
}

/* ── JSON 转义 ──────────────────────────────────────────────────
 * 把 src 中的特殊字符转义后写入 dst，返回写入长度
 */
static int json_escape(const char *src, char *dst, int dst_size) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 2; i++) {
        switch (src[i]) {
        case '"':  dst[j++] = '\\'; dst[j++] = '"';  break;
        case '\\': dst[j++] = '\\'; dst[j++] = '\\'; break;
        case '\n': dst[j++] = '\\'; dst[j++] = 'n';  break;
        case '\r': dst[j++] = '\\'; dst[j++] = 'r';  break;
        case '\t': dst[j++] = '\\'; dst[j++] = 't';  break;
        default:   dst[j++] = src[i]; break;
        }
    }
    dst[j] = '\0';
    return j;
}

/* ── 执行（带超时和输出捕获，固定缓冲区，无需 free） ─────── */
sandbox_result_t sandbox_exec(const char *command, int timeout_sec) {
    sandbox_result_t result;
    memset(&result, 0, sizeof(result));
    result.exit_code = -1;

    if (!command) return result;

    if (!sandbox_allowed(command)) {
        result.exit_code = 403;
        snprintf(result.stderr_buf, SANDBOX_MAX_STDERR,
            "TORK Sandbox: command not authorized at current permission level.");
        return result;
    }

    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0)
        return result;

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return result;
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        execl("/bin/sh", "sh", "-c", command, NULL);
        _exit(255);
    }

    /* Parent */
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    int stdout_fd = stdout_pipe[0];
    int stderr_fd = stderr_pipe[0];
    fcntl(stdout_fd, F_SETFL, fcntl(stdout_fd, F_GETFL) | O_NONBLOCK);
    fcntl(stderr_fd, F_SETFL, fcntl(stderr_fd, F_GETFL) | O_NONBLOCK);

    int stdout_pos = 0, stderr_pos = 0;
    int max_fd = (stdout_fd > stderr_fd) ? stdout_fd : stderr_fd;
    int remaining = timeout_sec * 10;
    int timed_out = 0;

    while (remaining > 0) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(stdout_fd, &rfds);
        FD_SET(stderr_fd, &rfds);

        struct timeval tv = {0, 100000};
        int ret = select(max_fd + 1, &rfds, NULL, NULL, &tv);

        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret > 0) {
            if (FD_ISSET(stdout_fd, &rfds)) {
                int n = read(stdout_fd, result.stdout_buf + stdout_pos,
                             SANDBOX_MAX_STDOUT - stdout_pos - 1);
                if (n > 0) stdout_pos += n;
            }
            if (FD_ISSET(stderr_fd, &rfds)) {
                int n = read(stderr_fd, result.stderr_buf + stderr_pos,
                             SANDBOX_MAX_STDERR - stderr_pos - 1);
                if (n > 0) stderr_pos += n;
            }
        }

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
        kill(pid, SIGKILL);
        for (;;) {
            pid_t wr = waitpid(pid, NULL, 0);
            if (wr == pid) break;
            if (wr < 0 && errno != EINTR) break;
        }
        timed_out = 1;
        result.exit_code = -1;
    }

    /* 排空残留输出 */
    char tmp[4096];
    int n;
    while ((n = read(stdout_fd, tmp, sizeof(tmp))) > 0) {
        if (stdout_pos + n < SANDBOX_MAX_STDOUT - 1) {
            memcpy(result.stdout_buf + stdout_pos, tmp, n);
            stdout_pos += n;
        }
    }
    while ((n = read(stderr_fd, tmp, sizeof(tmp))) > 0) {
        if (stderr_pos + n < SANDBOX_MAX_STDERR - 1) {
            memcpy(result.stderr_buf + stderr_pos, tmp, n);
            stderr_pos += n;
        }
    }

    close(stdout_fd);
    close(stderr_fd);

    result.stdout_buf[stdout_pos] = '\0';
    result.stderr_buf[stderr_pos] = '\0';
    result.timed_out = timed_out;

    return result;
}

/* ── JSON 格式执行（调用者需 free 返回值） ──────────────────── */
char *sandbox_exec_json(const char *command, int timeout_sec) {
    sandbox_result_t r = sandbox_exec(command, timeout_sec);

    char stdout_esc[SANDBOX_MAX_STDOUT * 2];
    char stderr_esc[SANDBOX_MAX_STDERR * 2];
    json_escape(r.stdout_buf, stdout_esc, sizeof(stdout_esc));
    json_escape(r.stderr_buf, stderr_esc, sizeof(stderr_esc));

    char *json = NULL;
    int ret = asprintf(&json,
        "{\"exit_code\":%d,\"stdout\":\"%s\",\"stderr\":\"%s\",\"timed_out\":%s}",
        r.exit_code, stdout_esc, stderr_esc,
        r.timed_out ? "true" : "false");
    (void)ret;

    return json;
}