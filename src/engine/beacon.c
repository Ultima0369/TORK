#include "beacon.h"
#include "soul_access.h"
#include "pi_seed.h"
#include "fractal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

/* ── 全局同类表 ───────────────────────────────────────── */
static peer_table_t g_peers;
static volatile int g_beacon_initialized = 0;
static int g_bcast_fd = -1;
static int g_listen_fd = -1;
static volatile int g_running = 0;
static uint8_t g_self_node_id[16];
static pthread_mutex_t g_self_node_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_listener_tid;

/* ── π-seed 异或摘要 ────────────────────────────────────
 * 最近 16 次 π-seed 采样的逐字节异或
 * 物理不可伪造：BBP 公式 + 晶振时序交叉
 * ──────────────────────────────────────────────────────── */
void pi_compute_digest(const uint32_t colony_seed[16], uint8_t digest[16]) {
    memset(digest, 0, 16);
    for (int i = 0; i < 16; i++) {
        digest[i % 4]     ^= (colony_seed[i]      ) & 0xFF;
        digest[(i%4)+4]   ^= (colony_seed[i] >>  8) & 0xFF;
        digest[(i%4)+8]   ^= (colony_seed[i] >> 16) & 0xFF;
        digest[(i%4)+12]  ^= (colony_seed[i] >> 24) & 0xFF;
    }
}

/* ── 信标验证 ───────────────────────────────────────────
 * 分形基元：比较·差异·模糊·类似
 * 不信任声明，只验证物理事实：
 * 1. magic 必须匹配
 * 2. tick 非零 (运行中的核心 tick > 0)
 * 3. TSC 低32位非零
 * 4. heartbeat_ms 必须在合法范围 [10, 5000]
 * 5. node_id 不能全零
 *
 * 五维向量，每维独立容忍，综合 delta > 0 则拒绝
 * ──────────────────────────────────────────────────────── */
static int beacon_verify(const beacon_frame_t *frame) {
    float input[5], reference[5], tolerance[5], weights[5];

    /* 1. magic: 必须精确匹配，不容忍 */
    input[0] = (ntohl(frame->magic) != BEACON_MAGIC) ? 1.0f : 0.0f;
    reference[0] = 0.0f;
    tolerance[0] = 0.0f;
    weights[0] = 10.0f;

    /* 2. tick: 非零，不容忍 */
    input[1] = (ntohl(frame->tick) == 0) ? 1.0f : 0.0f;
    reference[1] = 0.0f;
    tolerance[1] = 0.0f;
    weights[1] = 5.0f;

    /* 3. TSC: 非零，不容忍 */
    input[2] = (ntohl(frame->tsc_lo) == 0) ? 1.0f : 0.0f;
    reference[2] = 0.0f;
    tolerance[2] = 0.0f;
    weights[2] = 3.0f;

    /* 4. heartbeat_ms: 在 [10, 5000]，偏离范围的程度 */
    uint16_t hb = ntohs(frame->heartbeat_ms);
    float hb_val = 0.0f;
    if (hb < 10) hb_val = (float)(10 - hb) / 10.0f;
    else if (hb > 5000) hb_val = (float)(hb - 5000) / 5000.0f;
    input[3] = hb_val;
    reference[3] = 0.0f;
    tolerance[3] = 0.1f;
    weights[3] = 2.0f;

    /* 5. node_id: 非全零 */
    int id_zero = 1;
    for (int i = 0; i < 16; i++) {
        if (frame->node_id[i] != 0) { id_zero = 0; break; }
    }
    input[4] = id_zero ? 1.0f : 0.0f;
    reference[4] = 0.0f;
    tolerance[4] = 0.0f;
    weights[4] = 5.0f;

    fractal_input_t finp = {
        .dims = 5, .input = input, .reference = reference,
        .tolerance = tolerance, .weights = weights
    };
    fractal_output_t fout = fractal_step(&finp, NULL, NULL);

    return (fout.delta > 0.0f) ? -1 : 0;
}

/* ── 同类表操作 ───────────────────────────────────────── */
static int peer_find(peer_table_t *t, const uint8_t node_id[16]) {
    for (int i = 0; i < t->count; i++) {
        if (memcmp(t->peers[i].node_id, node_id, 16) == 0)
            return i;
    }
    return -1;
}

static void peer_fill(peer_entry_t *p, const beacon_frame_t *frame) {
    memcpy(p->node_id, frame->node_id, 16);
    p->last_tick    = ntohl(frame->tick);
    p->last_crc32   = ntohl(frame->crc32_prefix);
    p->last_tsc_lo  = ntohl(frame->tsc_lo);
    p->heartbeat_ms = ntohs(frame->heartbeat_ms);
    p->last_seen    = time(NULL);
    p->verified     = 1;
    memcpy(p->pi_digest, frame->pi_digest, 16);
    /* 信任分数: pi_digest 非全零 → +50, 已验证 → +50 */
    int digest_nonzero = 0;
    for (int i = 0; i < 16; i++) if (frame->pi_digest[i]) { digest_nonzero = 1; break; }
    p->trust_score = (digest_nonzero ? 50 : 0) + (p->verified ? 50 : 0);
}

static void peer_upsert(peer_table_t *t, const beacon_frame_t *frame) {
    pthread_mutex_lock(&t->lock);

    int idx = peer_find(t, frame->node_id);
    if (idx >= 0) {
        peer_fill(&t->peers[idx], frame);
    } else if (t->count < PEER_MAX) {
        peer_fill(&t->peers[t->count++], frame);
    }

    pthread_mutex_unlock(&t->lock);
}

void beacon_prune(peer_table_t *t) {
    if (!t) t = &g_peers;
    time_t now = time(NULL);
    pthread_mutex_lock(&t->lock);
    int write = 0;
    for (int read = 0; read < t->count; read++) {
        if (now - t->peers[read].last_seen < BEACON_EXPIRE_S) {
            if (write != read)
                t->peers[write] = t->peers[read];
            write++;
        }
    }
    t->count = write;
    pthread_mutex_unlock(&t->lock);
}

int beacon_peer_count(peer_table_t *t) {
    pthread_mutex_lock(&t->lock);
    int c = t->count;
    pthread_mutex_unlock(&t->lock);
    return c;
}

int beacon_global_count(void) {
    pthread_mutex_lock(&g_peers.lock);
    if (!g_beacon_initialized) return 0;
    int c = g_peers.count;
    pthread_mutex_unlock(&g_peers.lock);
    return c;
}

/* ── 广播信标 ─────────────────────────────────────────── */
int beacon_broadcast(const soul_t *soul,
                     const uint8_t pi_digest[16],
                     uint32_t tsc_lo) {
    if (g_bcast_fd < 0)
        return -1;

    beacon_frame_t frame;
    memset(&frame, 0, sizeof(frame));

    frame.magic         = htonl(BEACON_MAGIC);
    frame.tick          = htonl(soul_tick(soul));
    frame.crc32_prefix  = htonl(soul_checksum(soul));
    frame.tsc_lo        = htonl(tsc_lo);
    frame.heartbeat_ms  = htons(soul_heartbeat_ms(soul));
    memcpy(frame.pi_digest, pi_digest, 16);
    memcpy(frame.node_id, soul->buf + S_NODE_ID, 16);

    /* 记住自身 node_id，供 listener 过滤回声 */
    pthread_mutex_lock(&g_self_node_lock);
    memcpy(g_self_node_id, soul->buf + S_NODE_ID, 16);
    pthread_mutex_unlock(&g_self_node_lock);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(BEACON_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    ssize_t n = sendto(g_bcast_fd, &frame, BEACON_SIZE, 0,
                       (struct sockaddr*)&addr, sizeof(addr));
    return (n == BEACON_SIZE) ? 0 : -1;
}

/* ── 监听线程 ─────────────────────────────────────────── */
void *beacon_listener(void *arg) {
    (void)arg;
    beacon_frame_t frame;

    while (g_running) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        ssize_t n = recvfrom(g_listen_fd, &frame, BEACON_SIZE, 0,
                             (struct sockaddr*)&from, &fromlen);
        if (n != BEACON_SIZE) {
            /* EINTR 或超时：检查 g_running 后重试 */
            if (!g_running) break;
            continue;
        }

        /* 忽略自己发出的信标 (node_id 匹配) */
        pthread_mutex_lock(&g_self_node_lock);
        int is_self = (memcmp(frame.node_id, g_self_node_id, 16) == 0);
        pthread_mutex_unlock(&g_self_node_lock);
        if (is_self)
            continue;

        if (beacon_verify(&frame) != 0)
            continue;

        peer_upsert(&g_peers, &frame);
    }
    return NULL;
}

/* ── 初始化 ───────────────────────────────────────────── */
int beacon_init(peer_table_t *table) {
    memset(&g_peers, 0, sizeof(g_peers));
    pthread_mutex_init(&g_peers.lock, NULL);

    /* 广播 socket */
    g_bcast_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_bcast_fd < 0) {
        perror("beacon: broadcast socket");
        return -1;
    }
    int bcast_enable = 1;
    if (setsockopt(g_bcast_fd, SOL_SOCKET, SO_BROADCAST,
                   &bcast_enable, sizeof(bcast_enable)) != 0) {
        perror("beacon: setsockopt SO_BROADCAST");
        close(g_bcast_fd); g_bcast_fd = -1;
        return -1;
    }

    /* 监听 socket */
    g_listen_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_listen_fd < 0) {
        perror("beacon: listen socket");
        close(g_bcast_fd); g_bcast_fd = -1;
        return -1;
    }
    int reuse = 1;
    if (setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) != 0) {
        perror("beacon: setsockopt SO_REUSEADDR");
        close(g_listen_fd); close(g_bcast_fd);
        g_listen_fd = -1; g_bcast_fd = -1;
        return -1;
    }

    /* 1秒超时：让 recvfrom 周期性返回，检查 g_running */
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(g_listen_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(BEACON_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(g_listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("beacon: bind");
        close(g_listen_fd); close(g_bcast_fd);
        g_listen_fd = -1; g_bcast_fd = -1;
        return -1;
    }

    /* 启动监听线程 (可 join，确保干净退出) */
    g_running = 1;
    if (pthread_create(&g_listener_tid, NULL, beacon_listener, NULL) != 0) {
        perror("beacon: listener thread");
        close(g_listen_fd); close(g_bcast_fd);
        g_listen_fd = -1; g_bcast_fd = -1;
        g_running = 0;
        return -1;
    }

    /* 拷贝同类表快照给调用者（不拷贝 mutex，单独初始化） */
    if (table) {
        pthread_mutex_lock(&g_peers.lock);
        memcpy(table->peers, g_peers.peers, sizeof(g_peers.peers));
        table->count = g_peers.count;
        pthread_mutex_unlock(&g_peers.lock);
        pthread_mutex_init(&table->lock, NULL);
    }

    g_beacon_initialized = 1;
    return 0;
}

void beacon_shutdown(void) {
    g_running = 0;

    /* 唤醒阻塞在 recvfrom 的监听线程 */
    if (g_listen_fd >= 0) shutdown(g_listen_fd, SHUT_RDWR);

    /* 等待监听线程退出 */
    pthread_join(g_listener_tid, NULL);

    if (g_listen_fd >= 0) { close(g_listen_fd); g_listen_fd = -1; }
    if (g_bcast_fd >= 0)  { close(g_bcast_fd);  g_bcast_fd = -1; }
    pthread_mutex_destroy(&g_peers.lock);
}
