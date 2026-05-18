#ifdef _WIN32

#include "win32.h"
#include <stdio.h>
#include <string.h>
#include <process.h>
#include <direct.h>

/* ── 进程管理 ────────────────────────────────────────────── */

/* 全局子进程跟踪 */
#define MAX_CHILD 64
static struct {
    tork_pid_t pid;
    int used;
} _children[MAX_CHILD];

static int _compat_initialized = 0;

tork_pid_t* tork_fork(void) {
    tork_pid_t *parent_pid = NULL;

    /* 分配子进程跟踪槽 */
    int slot = -1;
    for (int i = 0; i < MAX_CHILD; i++) {
        if (!_children[i].used) { slot = i; break; }
    }
    if (slot < 0) return NULL;

    /* 获取当前可执行路径 */
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);

    /* 构建命令行 — 添加 --child 标志表示这是子进程 */
    char cmdline[1024];
    snprintf(cmdline, sizeof(cmdline), "\"%s\" --child", exe_path);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);

    if (!CreateProcessA(exe_path, cmdline, NULL, NULL, FALSE,
                        CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        return NULL;
    }

    /* 父进程: 记录子进程信息 */
    _children[slot].used = 1;
    _children[slot].pid.hProcess = pi.hProcess;
    _children[slot].pid.hThread = pi.hThread;
    _children[slot].pid.dwProcessId = pi.dwProcessId;
    _children[slot].pid.exit_code = 0;
    _children[slot].pid.valid = 1;

    CloseHandle(pi.hThread);
    return &_children[slot].pid;
}

int tork_waitpid(tork_pid_t *pid, int *status, int options) {
    if (!pid || !pid->valid) return -1;

    DWORD wait = WaitForSingleObject(pid->hProcess, INFINITE);
    if (wait == WAIT_OBJECT_0) {
        DWORD exit = 0;
        GetExitCodeProcess(pid->hProcess, &exit);
        pid->exit_code = (int)exit;
        if (status) *status = exit;
        CloseHandle(pid->hProcess);
        pid->valid = 0;

        /* 清理跟踪槽 */
        for (int i = 0; i < MAX_CHILD; i++) {
            if (&_children[i].pid == pid) {
                _children[i].used = 0;
                break;
            }
        }
        return (int)exit;
    }
    return -1;
}

int tork_kill(tork_pid_t *pid, int sig) {
    if (!pid || !pid->valid) return -1;

    if (sig == TORK_SIGTERM || sig == TORK_SIGINT) {
        TerminateProcess(pid->hProcess, 1);
        return 0;
    }
    /* SIGUSR1/SIGUSR2: 用 Ctrl+C 模拟 */
    GenerateConsoleCtrlEvent(CTRL_C_EVENT, pid->dwProcessId);
    return 0;
}

int tork_getpid(void) {
    return (int)GetCurrentProcessId();
}

/* ── 信号处理 ────────────────────────────────────────────── */

static void (*_sig_handlers[32])(int) = { NULL };

static BOOL WINAPI _console_ctrl_handler(DWORD ctrlType) {
    int sig = TORK_SIGTERM;
    switch (ctrlType) {
        case CTRL_C_EVENT:      sig = TORK_SIGINT;  break;
        case CTRL_BREAK_EVENT:  sig = TORK_SIGTERM; break;
        case CTRL_CLOSE_EVENT:  sig = TORK_SIGTERM; break;
        default: return FALSE;
    }
    if (_sig_handlers[sig]) {
        _sig_handlers[sig](sig);
        return TRUE;
    }
    return FALSE;
}

tork_sighandler_t tork_signal(int sig, tork_sighandler_t handler) {
    tork_sighandler_t old = NULL;
    if (sig >= 0 && sig < 32) {
        old = _sig_handlers[sig];
        _sig_handlers[sig] = handler;
    }
    /* 注册控制台事件处理器 */
    SetConsoleCtrlHandler(_console_ctrl_handler, TRUE);
    return old;
}

int tork_raise(int sig) {
    if (_sig_handlers[sig]) {
        _sig_handlers[sig](sig);
        return 0;
    }
    return -1;
}

/* ── 管道 ────────────────────────────────────────────────── */

int tork_pipe(int fds[2]) {
    /* 使用命名管道模拟 */
    char pipename[64];
    snprintf(pipename, sizeof(pipename), "\\\\.\\pipe\\tork_pipe_%d_%d",
             GetCurrentProcessId(), rand());

    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    hRead = CreateNamedPipeA(pipename,
                             PIPE_ACCESS_INBOUND,
                             PIPE_TYPE_BYTE | PIPE_WAIT,
                             1, 4096, 4096, 0, &sa);
    if (hRead == INVALID_HANDLE_VALUE) return -1;

    hWrite = CreateFileA(pipename, GENERIC_WRITE, 0, &sa,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hWrite == INVALID_HANDLE_VALUE) {
        CloseHandle(hRead);
        return -1;
    }

    /* 将句柄转为 fd (用 _open_osfhandle) */
    fds[0] = _open_osfhandle((intptr_t)hRead, _O_RDONLY);
    fds[1] = _open_osfhandle((intptr_t)hWrite, _O_WRONLY);
    return 0;
}

int tork_close(int fd) {
    return _close(fd);
}

/* ── 内存映射 ────────────────────────────────────────────── */

void* tork_mmap(void *addr, size_t length, int prot, int flags,
                int fd, size_t offset) {
    HANDLE hMap = CreateFileMappingA(
        (HANDLE)_get_osfhandle(fd),
        NULL, PAGE_READWRITE, 0, (DWORD)length, NULL);
    if (!hMap) return NULL;

    void *map = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, (DWORD)offset, length);
    CloseHandle(hMap);
    return map;
}

int tork_munmap(void *addr, size_t length) {
    return UnmapViewOfFile(addr) ? 0 : -1;
}

/* ── 网络 ────────────────────────────────────────────────── */

int tork_net_init(void) {
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
}

void tork_net_cleanup(void) {
    WSACleanup();
}

int tork_socket(int domain, int type, int protocol) {
    return (int)socket(domain, type, protocol);
}

int tork_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    return connect(sockfd, addr, addrlen);
}

int tork_send(int sockfd, const void *buf, size_t len, int flags) {
    return send(sockfd, buf, (int)len, flags);
}

int tork_recv(int sockfd, void *buf, size_t len, int flags) {
    return recv(sockfd, buf, (int)len, flags);
}

int tork_closesocket(int sockfd) {
    return closesocket(sockfd);
}

/* ── 文件系统 ────────────────────────────────────────────── */

int tork_mkdir(const char *path) {
    return _mkdir(path);
}

int tork_unlink(const char *path) {
    return _unlink(path);
}

int tork_access(const char *path, int mode) {
    return _access(path, mode);
}

/* ── 线程 ────────────────────────────────────────────────── */

typedef struct {
    void *(*func)(void*);
    void *arg;
} _thread_start_t;

static unsigned int __stdcall _thread_start(void *arg) {
    _thread_start_t *start = (_thread_start_t*)arg;
    start->func(start->arg);
    free(start);
    return 0;
}

int tork_thread_create(tork_thread_t *thread, void *(*start_routine)(void*),
                       void *arg) {
    _thread_start_t *start = malloc(sizeof(_thread_start_t));
    if (!start) return -1;
    start->func = start_routine;
    start->arg = arg;

    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, _thread_start, start, 0, NULL);
    if (!h) { free(start); return -1; }
    *thread = h;
    return 0;
}

int tork_thread_join(tork_thread_t thread, void **retval) {
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    if (retval) *retval = NULL;
    return 0;
}

/* ── 动态库 ────────────────────────────────────────────── */

void* tork_dlopen(const char *filename) {
    return (void*)LoadLibraryA(filename);
}

void* tork_dlsym(void *handle, const char *symbol) {
    return (void*)GetProcAddress((HMODULE)handle, symbol);
}

int tork_dlclose(void *handle) {
    return FreeLibrary((HMODULE)handle) ? 0 : -1;
}

/* ── 目录遍历 ────────────────────────────────────────────── */

tork_dir_t* tork_opendir(const char *path) {
    tork_dir_t *dir = calloc(1, sizeof(tork_dir_t));
    if (!dir) return NULL;

    char pattern[260];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    dir->hFind = FindFirstFileA(pattern, &dir->findData);
    if (dir->hFind == INVALID_HANDLE_VALUE) {
        free(dir);
        return NULL;
    }
    dir->first = 1;
    strncpy(dir->current, dir->findData.cFileName, sizeof(dir->current) - 1);
    return dir;
}

const char* tork_readdir(tork_dir_t *dir) {
    if (!dir) return NULL;
    if (dir->first) {
        dir->first = 0;
    } else {
        if (!FindNextFileA(dir->hFind, &dir->findData)) return NULL;
        strncpy(dir->current, dir->findData.cFileName, sizeof(dir->current) - 1);
    }
    return dir->current;
}

int tork_closedir(tork_dir_t *dir) {
    if (!dir) return -1;
    FindClose(dir->hFind);
    free(dir);
    return 0;
}

/* ── 时间/睡眠 ────────────────────────────────────────────── */

int tork_usleep(unsigned long usec) {
    Sleep((DWORD)(usec / 1000));
    return 0;
}

int tork_nanosleep(unsigned long sec, unsigned long nsec) {
    DWORD ms = (DWORD)(sec * 1000 + nsec / 1000000);
    Sleep(ms);
    return 0;
}

int tork_gettimeofday(void *tv, void *tz) {
    /* 简化实现 */
    if (tv) {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        t -= 116444736000000000LL; /* 1601→1970 偏移 */
        t /= 10; /* 100ns → μs */
        ((uint32_t*)tv)[0] = (uint32_t)(t / 1000000);
        ((uint32_t*)tv)[1] = (uint32_t)(t % 1000000);
    }
    return 0;
}

/* ── 统一入口 ────────────────────────────────────────────── */

void tork_compat_init(void) {
    if (_compat_initialized) return;
    _compat_initialized = 1;

    memset(_children, 0, sizeof(_children));

    /* 初始化 Winsock */
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    /* 注册控制台事件处理器 */
    SetConsoleCtrlHandler(_console_ctrl_handler, TRUE);

    /* 设置默认信号处理器 */
    _sig_handlers[TORK_SIGINT] = NULL;
    _sig_handlers[TORK_SIGTERM] = NULL;
}

void tork_compat_cleanup(void) {
    /* 清理所有子进程 */
    for (int i = 0; i < MAX_CHILD; i++) {
        if (_children[i].used && _children[i].pid.valid) {
            TerminateProcess(_children[i].pid.hProcess, 0);
            CloseHandle(_children[i].pid.hProcess);
            _children[i].used = 0;
        }
    }
    WSACleanup();
}

void tork_exit(int code) {
    tork_compat_cleanup();
    ExitProcess(code);
}

#endif /* _WIN32 */
