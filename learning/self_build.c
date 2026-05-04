#include "self_build.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

static self_build_t g_sb;
static int g_initialized = 0;

/* ── 递归扫描源文件 ────────────────────────────────────────── */
static void scan_dir(const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) return;
    
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && g_sb.source_count < SB_MAX_SOURCES) {
        if (entry->d_name[0] == '.') continue;
        
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", dir_path, entry->d_name);
        
        if (entry->d_type == DT_DIR) {
            scan_dir(full);
        } else {
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0 || 
                        strcmp(ext, ".asm") == 0 || strcmp(ext, ".py") == 0 ||
                        strcmp(ext, ".sh") == 0)) {
                sb_source_t *s = &g_sb.sources[g_sb.source_count++];
                snprintf(s->path, sizeof(s->path), "%s", full);
                struct stat st;
                if (stat(full, &st) == 0) {
                    s->mtime = st.st_mtime;
                }
                s->changed = 0;
            }
        }
    }
    closedir(d);
}

/* ── 初始化 ────────────────────────────────────────────────── */
void sb_init(void) {
    memset(&g_sb, 0, sizeof(g_sb));
    g_sb.state = SB_MONITORING;
    g_sb.interval_ticks = 50;  /* 每50tick检查一次 */
    g_sb.auto_build = 1;       /* 默认自动编译 */
    
    /* 扫描项目源文件 */
    scan_dir(".");
    printf("  SELF: monitoring %d source files\n", g_sb.source_count);
    g_initialized = 1;
}

/* ── 检查源文件变更 ────────────────────────────────────────── */
int sb_check_sources(void) {
    if (!g_initialized) return 0;
    
    int changed = 0;
    for (uint32_t i = 0; i < g_sb.source_count; i++) {
        sb_source_t *s = &g_sb.sources[i];
        struct stat st;
        if (stat(s->path, &st) == 0) {
            if (st.st_mtime != s->mtime) {
                s->changed = 1;
                s->mtime = st.st_mtime;
                changed++;
            }
        }
    }
    
    if (changed > 0 && g_sb.auto_build && g_sb.state == SB_MONITORING) {
        g_sb.state = SB_BUILDING;
        printf("  SELF: %d files changed, starting build...\n", changed);
        snprintf(g_sb.build_log, sizeof(g_sb.build_log),
                 "[%ld] %d files changed", time(NULL), changed);
        sb_build();
    }
    
    return changed;
}

/* ── 执行编译 ──────────────────────────────────────────────── */
int sb_build(void) {
    if (!g_initialized) return -1;
    
    g_sb.state = SB_BUILDING;
    g_sb.build_count++;
    
    /* 更具体的 Build 日志 */
    __attribute__((unused)) int prev_len = strlen(g_sb.build_log);
    
    /* Run make with output capture */
    int pipe_fd[2];
    if (pipe(pipe_fd) < 0) {
        g_sb.state = SB_FAILED;
        g_sb.fail_count++;
        return -1;
    }
    
    pid_t pid = fork();
    if (pid == 0) {
        /* Child: run make, redirect stdout/stderr to pipe */
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);
        
        execlp("make", "make", "all", NULL);
        _exit(1);
    } else if (pid > 0) {
        close(pipe_fd[1]);
        
        /* Read build output */
        char buf[256];
        ssize_t n;
        while ((n = read(pipe_fd[0], buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            /* Append last line to build_log */
            if (strlen(g_sb.build_log) < sizeof(g_sb.build_log) - 100) {
                // Only keep last meaningful line
                char *last_line = strrchr(buf, '\n');
                if (last_line) {
                    *last_line = '\0';
                    last_line = strrchr(buf, '\n');
                    if (last_line) {
                        snprintf(g_sb.build_log + strlen(g_sb.build_log),
                                 sizeof(g_sb.build_log) - strlen(g_sb.build_log),
                                 " | %s", last_line + 1);
                    }
                }
            }
        }
        close(pipe_fd[0]);
        
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            g_sb.state = SB_SUCCESS;
            g_sb.success_count++;
            printf("  SELF: build SUCCESS (build #%d)\n", g_sb.build_count);
            
            /* Auto hotswap on success */
            sb_hotswap();
            
            /* Reset changed flags */
            for (uint32_t i = 0; i < g_sb.source_count; i++) {
                g_sb.sources[i].changed = 0;
            }
            
            return 0;
        } else {
            g_sb.state = SB_FAILED;
            g_sb.fail_count++;
            printf("  SELF: build FAILED (build #%d)\n", g_sb.build_count);
            return -1;
        }
    }
    
    g_sb.state = SB_MONITORING;
    return -1;
}

/* ── 热更新 ────────────────────────────────────────────────── */
int sb_hotswap(void) {
    if (!g_initialized) return -1;
    
    /* Verify new binary exists */
    struct stat st;
    if (stat("build/tork_engine", &st) != 0) {
        printf("  SELF: hotswap skipped (no new binary)\n");
        return -1;
    }
    
    /* Copy to a safe backup location */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "cp build/tork_engine build/tork_engine.new 2>/dev/null");
    system(cmd);
    
    /* Signal the running engine to reload on next tick */
    /* We write a special file that tork_engine checks each loop */
    FILE *f = fopen("/tmp/tork_hotswap_ready", "w");
    if (f) {
        fprintf(f, "build/tork_engine.new\n");
        fclose(f);
        printf("  SELF: hotswap signal sent (binary ready at build/tork_engine.new)\n");
        return 0;
    }
    
    return -1;
}

/* ── 获取摘要 ──────────────────────────────────────────────── */
void sb_summary(char *buf, int buf_size) {
    if (!g_initialized || !buf || buf_size < 1) return;
    
    const char *state_names[] = {
        "idle", "monitoring", "building", "testing", "success", "failed"
    };
    
    snprintf(buf, buf_size,
        "SELF: state=%s sources=%u builds=%u ok=%u fail=%u auto=%s",
        state_names[g_sb.state],
        g_sb.source_count,
        g_sb.build_count, g_sb.success_count, g_sb.fail_count,
        g_sb.auto_build ? "on" : "off");
}

/* ── 保存/加载 ────────────────────────────────────────────── */
int sb_save(void) {
    if (!g_initialized) return -1;
    FILE *f = fopen("persist/self_build.bin", "wb");
    if (!f) return -1;
    fwrite(&g_sb, sizeof(g_sb), 1, f);
    fclose(f);
    return 0;
}

int sb_load(void) {
    if (!g_initialized) sb_init();
    FILE *f = fopen("persist/self_build.bin", "rb");
    if (!f) return -1;
    fread(&g_sb, sizeof(g_sb), 1, f);
    fclose(f);
    printf("  SELF: loaded state (builds=%d ok=%d fail=%d)\n", 
           g_sb.build_count, g_sb.success_count, g_sb.fail_count);
    return 0;
}

self_build_t* sb_get_state(void) {
    if (!g_initialized) return NULL;
    return &g_sb;
}

void sb_set_auto(int enabled) {
    if (!g_initialized) return;
    g_sb.auto_build = enabled;
    printf("  SELF: auto-build %s\n", enabled ? "enabled" : "disabled");
}
