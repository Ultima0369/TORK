#ifndef WIN32_COMPAT_H
#define WIN32_COMPAT_H

/* ── TORK Windows 原生兼容层 ──────────────────────────────
 *  将 POSIX syscall (fork/waitpid/kill/signal/pipe) 映射到
 *  Win32 API (CreateProcess/WaitForSingleObject/TerminateProcess/...)
 *
 *  在 Windows 上编译时: -D_WIN32
 *  在 Linux 上编译时: 透明透传到底层 POSIX
 * ──────────────────────────────────────────────────────────── */

#ifdef _WIN32

#include <windows.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 进程管理 ────────────────────────────────────────────── */

/* TORK 进程句柄 (替代 pid_t) */
typedef struct {
    HANDLE  hProcess;
    HANDLE  hThread;
    DWORD   dwProcessId;
    int     exit_code;
    int     valid;
} tork_pid_t;

/* tork_fork() — 替代 fork()
 * 返回值: 父进程收到子进程 tork_pid_t (valid=1)
 *         子进程收到自己的 pid (hProcess=NULL, dwProcessId=GetCurrentProcessId())
 *         失败返回 NULL */
tork_pid_t* tork_fork(void);

/* tork_waitpid() — 替代 waitpid()
 * 等待指定 TORK 进程退出 */
int tork_waitpid(tork_pid_t *pid, int *status, int options);

/* tork_kill() — 替代 kill()
 * 向 TORK 进程发送信号 */
int tork_kill(tork_pid_t *pid, int sig);

/* tork_getpid() — 替代 getpid() */
int tork_getpid(void);

/* ── 信号处理 ────────────────────────────────────────────── */

/* TORK 信号常量 */
#define TORK_SIGINT     2
#define TORK_SIGTERM    15
#define TORK_SIGUSR1    10
#define TORK_SIGUSR2    12
#define TORK_SIGCHLD    17

typedef void (*tork_sighandler_t)(int);

/* tork_signal() — 替代 signal()
 * 注册信号处理器 */
tork_sighandler_t tork_signal(int sig, tork_sighandler_t handler);

/* tork_raise() — 替代 raise() */
int tork_raise(int sig);

/* ── 管道/IPC ────────────────────────────────────────────── */

/* tork_pipe() — 替代 pipe()
 * 返回 0 成功, -1 失败 */
int tork_pipe(int fds[2]);

/* tork_close() — 替代 close() */
int tork_close(int fd);

/* ── 内存映射 ────────────────────────────────────────────── */

/* tork_mmap() — 替代 mmap() */
void* tork_mmap(void *addr, size_t length, int prot, int flags,
                int fd, size_t offset);

/* tork_munmap() — 替代 munmap() */
int tork_munmap(void *addr, size_t length);

/* ── 网络 ────────────────────────────────────────────────── */

/* Winsock 初始化/清理 */
int tork_net_init(void);
void tork_net_cleanup(void);

/* tork_socket() — 替代 socket() */
int tork_socket(int domain, int type, int protocol);

/* tork_connect() — 替代 connect() */
int tork_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

/* tork_send() / tork_recv() — 替代 send() / recv() */
int tork_send(int sockfd, const void *buf, size_t len, int flags);
int tork_recv(int sockfd, void *buf, size_t len, int flags);

/* tork_closesocket() — 替代 close() (对 socket) */
int tork_closesocket(int sockfd);

/* ── 文件系统 ────────────────────────────────────────────── */

/* tork_mkdir() — 替代 mkdir() */
int tork_mkdir(const char *path);

/* tork_unlink() — 替代 unlink() */
int tork_unlink(const char *path);

/* tork_access() — 替代 access() */
int tork_access(const char *path, int mode);

/* ── 线程 ────────────────────────────────────────────────── */

typedef HANDLE tork_thread_t;

/* tork_thread_create() — 替代 pthread_create() */
int tork_thread_create(tork_thread_t *thread, void *(*start_routine)(void*),
                       void *arg);

/* tork_thread_join() — 替代 pthread_join() */
int tork_thread_join(tork_thread_t thread, void **retval);

/* ── 动态库 ────────────────────────────────────────────── */

/* tork_dlopen() — 替代 dlopen() */
void* tork_dlopen(const char *filename);

/* tork_dlsym() — 替代 dlsym() */
void* tork_dlsym(void *handle, const char *symbol);

/* tork_dlclose() — 替代 dlclose() */
int tork_dlclose(void *handle);

/* ── 目录遍历 ────────────────────────────────────────────── */

typedef struct {
    HANDLE          hFind;
    WIN32_FIND_DATA findData;
    char            current[260];
    int             first;
} tork_dir_t;

/* tork_opendir() — 替代 opendir() */
tork_dir_t* tork_opendir(const char *path);

/* tork_readdir() — 替代 readdir() */
const char* tork_readdir(tork_dir_t *dir);

/* tork_closedir() — 替代 closedir() */
int tork_closedir(tork_dir_t *dir);

/* ── 时间/睡眠 ────────────────────────────────────────────── */

/* tork_usleep() — 替代 usleep() */
int tork_usleep(unsigned long usec);

/* tork_nanosleep() — 替代 nanosleep() */
int tork_nanosleep(unsigned long sec, unsigned long nsec);

/* tork_gettimeofday() — 替代 gettimeofday() */
int tork_gettimeofday(void *tv, void *tz);

/* ── 统一入口: 程序启动时调用一次 ────────────────────────── */
void tork_compat_init(void);
void tork_compat_cleanup(void);

/* ── TORK 进程退出 ────────────────────────────────────────── */
void tork_exit(int code);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */

#endif /* WIN32_COMPAT_H */
