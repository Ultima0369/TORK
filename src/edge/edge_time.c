/* ══════════════════════════════════════════════════════════════
 * TORK 边缘时间同步 — NTP 客户端 (嵌入式紧凑版)
 *
 * 树莓派 Zero 没有 RTC，重启后时间归零 (1970-01-01)。
 * 此模块在启动时通过 NTP 获取 UTC 时间，校正 edge_predictor 的时间基准。
 *
 * UDP 发包只用一次握手（无状态），最小化资源消耗。
 * ══════════════════════════════════════════════════════════════ */

#include "edge_sensor.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

/* ── NTP 包格式 ────────────────────────────────────────── */
typedef struct {
    uint8_t  flags;        /* LI + VN + Mode */
    uint8_t  stratum;
    uint8_t  poll;
    int8_t   precision;
    uint32_t root_delay;
    uint32_t root_dispersion;
    uint32_t ref_id;
    uint32_t ref_ts_sec;
    uint32_t ref_ts_frac;
    uint32_t orig_ts_sec;
    uint32_t orig_ts_frac;
    uint32_t rx_ts_sec;
    uint32_t rx_ts_frac;
    uint32_t tx_ts_sec;
    uint32_t tx_ts_frac;
} __attribute__((packed)) ntp_packet_t;

#define NTP_PORT     123
#define NTP_TIMEOUT  5000    /* 5 秒超时 */
#define NTP_EPOCH    2208988800UL  /* 1900→1970 秒偏移 */

/* ── NTP 服务器列表 ────────────────────────────────────── */
static const char *ntp_servers[] = {
    "pool.ntp.org",
    "time.google.com",
    "time.cloudflare.com",
    NULL
};

/* ── 内部状态 ──────────────────────────────────────────── */
static int g_ntp_synced = 0;
static uint32_t g_ntp_offset_ms = 0;  /* NTP 时间 - 本地单调时间的偏移 (ms) */

/* ── NTP 请求 ──────────────────────────────────────────── */
static int ntp_request(const char *hostname, uint32_t *out_sec, uint32_t *out_usec) {
#ifdef __linux__
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NTP_PORT);

    struct hostent *he = gethostbyname(hostname);
    if (!he) { close(sock); return -1; }
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    /* 构造 NTP 请求 (mode 3 = client) */
    ntp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.flags = 0x1B;  /* LI=0, VN=3, Mode=3 */

    struct timeval tv;
    gettimeofday(&tv, NULL);
    pkt.tx_ts_sec  = htonl(tv.tv_sec + NTP_EPOCH);
    pkt.tx_ts_frac = htonl((uint32_t)((double)tv.tv_usec * 4294.967296));

    if (sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock); return -1;
    }

    /* 带超时的接收 */
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    struct timeval timeout = { NTP_TIMEOUT / 1000, (NTP_TIMEOUT % 1000) * 1000 };
    int sel = select(sock + 1, &fds, NULL, NULL, &timeout);
    if (sel <= 0) { close(sock); return -1; }

    socklen_t addr_len = sizeof(addr);
    if (recvfrom(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&addr, &addr_len) < 0) {
        close(sock); return -1;
    }
    close(sock);

    pkt.rx_ts_sec  = ntohl(pkt.rx_ts_sec);
    pkt.rx_ts_frac = ntohl(pkt.rx_ts_frac);
    pkt.tx_ts_sec  = ntohl(pkt.tx_ts_sec);
    pkt.tx_ts_frac = ntohl(pkt.tx_ts_frac);

    /* 计算往返延迟和服务器时间 */
    uint32_t t   = pkt.rx_ts_sec - NTP_EPOCH;   /* 服务器时间 (秒) */
    uint32_t tus = (uint32_t)((double)pkt.rx_ts_frac / 4294.967296);

    *out_sec  = t;
    *out_usec = tus;
    return 0;
#else
    (void)hostname; (void)out_sec; (void)out_usec;
    return -1;
#endif
}

/* ── 同步 NTP 时间 ──────────────────────────────────────── */
int edge_sync_ntp(void) {
    if (g_ntp_synced) return 0;

    uint32_t sec = 0, usec = 0;
    int ok = -1;

    for (int i = 0; ntp_servers[i]; i++) {
        if (ntp_request(ntp_servers[i], &sec, &usec) == 0) {
            ok = 0;
            printf("  EDGE TIME: synced via %s → %u.%06u UTC\n",
                   ntp_servers[i], sec, usec);
            break;
        }
    }

    if (ok != 0) {
        printf("  EDGE TIME: NTP sync failed, using monotonic clock\n");
        return -1;
    }

    /* 计算偏移: NTP 时间 - 单调时钟 */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint32_t mono_ms = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    uint32_t ntp_ms  = sec * 1000 + usec / 1000;

    g_ntp_offset_ms = ntp_ms - mono_ms;
    g_ntp_synced = 1;
    return 0;
}

/* ── 获取校正后的 UTC 时间戳 (ms) ─────────────────────── */
uint64_t edge_utc_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t mono_ms = (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);

    if (g_ntp_synced)
        return mono_ms + g_ntp_offset_ms;
    return mono_ms;  /* 回退到单调时钟 */
}

/* ── 状态查询 ──────────────────────────────────────────── */
int edge_time_synced(void) {
    return g_ntp_synced;
}

/* ── 转换: 时间戳 → 可读字符串 ────────────────────────── */
void edge_time_str(uint64_t utc_ms, char *buf, size_t buf_size) {
    if (!buf || buf_size < 2) return;
    time_t t = (time_t)(utc_ms / 1000);
    struct tm *tm = gmtime(&t);
    if (tm) {
        strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S UTC", tm);
    } else {
        snprintf(buf, buf_size, "%llu", (unsigned long long)utc_ms);
    }
}
