#include "distributed.h"
#include "experience.h"
#include "pattern.h"
#include "mutation_guide.h"
#include "pi_seed.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

/* ── 简版 CRC32 (多项式 0xEDB88320, 和 asm 核心一致) ───── */
static uint32_t dist_crc32(const void *data, size_t len) {
    static const uint32_t table[256] = {
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
        0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
        0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
        0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
        0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
        0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
        0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
        0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
        0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
        0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
        0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
        0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
        0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
        0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
        0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
        0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
        0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
        0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
        0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
        0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
        0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
        0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
        0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
        0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
        0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
        0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
        0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
        0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
        0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
        0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
        0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
        0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
        0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
        0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
        0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
        0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
        0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
        0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
    };
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *d = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++)
        crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

/* ── 状态 ───────────────────────────────────────────────── */
static int g_sock = -1;
static uint32_t g_instance_id = 0;
static int g_initialized = 0;
static time_t g_last_heartbeat = 0;
static time_t g_last_broadcast = 0;
static int g_peer_count = 0;
static uint32_t g_seen_peers[64];
static time_t g_peer_time[64];

/* ── 初始化 ──────────────────────────────────────────────── */
int dist_init(void) {
    if (g_initialized) return 0;
    
    /* Generate instance ID from PID + time */
    pi_seed_init();
    g_instance_id = (uint32_t)(pi_seed_from_tsc() ^ getpid() ^ (time(NULL) & 0xFFFFFFFF));
    if (g_instance_id == 0) g_instance_id = 1;
    
    /* Create UDP socket */
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock < 0) {
        /* Network unavailable — non-fatal */
        g_sock = -1;
        g_initialized = 0;
        return -1;
    }
    
    /* Allow reuse */
    int reuse = 1;
    setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    /* Bind to port */
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(DIST_MCAST_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(g_sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        close(g_sock);
        g_sock = -1;
        return -1;
    }
    
    /* Join multicast group */
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(DIST_MCAST_GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    
    if (setsockopt(g_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        /* Multicast may not be available — socket still usable for broadcast */
        /* Non-fatal */
    }
    
    /* Set non-blocking */
    int flags = fcntl(g_sock, F_GETFL, 0);
    fcntl(g_sock, F_SETFL, flags | O_NONBLOCK);
    
    /* Set TTL=1 (local subnet only) */
    int ttl = 1;
    setsockopt(g_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    
    g_initialized = 1;
    printf("  DIST: distributed blackboard ready (%s:%d, ID=0x%08X)\n",
           DIST_MCAST_GROUP, DIST_MCAST_PORT, g_instance_id);
    
    return 0;
}

/* ── 发送一个原始消息 ────────────────────────────────────── */
static void dist_send(uint8_t msg_type, const void *payload, uint16_t payload_len) {
    if (g_sock < 0) return;
    
    uint8_t buf[DIST_MAX_MSG];
    dist_header_t *hdr = (dist_header_t*)buf;
    
    hdr->magic = htonl(DIST_APP_ID);
    hdr->token = htonl(DIST_TOKEN);
    hdr->version = 1;
    hdr->msg_type = msg_type;
    hdr->instance_id = htonl(g_instance_id);
    hdr->gen_count = htonl(0);  /* Will be set if needed */
    hdr->payload_len = htons(payload_len);
    
    if (payload && payload_len > 0)
        memcpy(buf + DIST_HEADER_SIZE, payload, payload_len);
    
    uint32_t total_len = DIST_HEADER_SIZE + payload_len;
    hdr->crc32 = 0;  /* zero before CRC calculation */
    hdr->crc32 = htonl(dist_crc32(buf, total_len));
    
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(DIST_MCAST_PORT);
    dest.sin_addr.s_addr = inet_addr(DIST_MCAST_GROUP);
    
    sendto(g_sock, buf, total_len, 0, (struct sockaddr*)&dest, sizeof(dest));
}

/* ── 广播经验 ────────────────────────────────────────────── */
void dist_broadcast_experience(uint8_t hw_stress, int8_t drive_pre,
                                uint8_t action_type, int8_t action_param,
                                int8_t outcome, uint8_t crash) {
    /* Rate limit: max 1 broadcast per 5 seconds */
    time_t now = time(NULL);
    if (now - g_last_broadcast < 5) return;
    g_last_broadcast = now;
    
    dist_experience_t exp;
    exp.hw_stress = hw_stress;
    exp.drive_pre = drive_pre;
    exp.action_type = action_type;
    exp.action_param = action_param;
    exp.outcome = outcome;
    exp.crash = crash;
    
    dist_send(DIST_MSG_EXPERIENCE, &exp, sizeof(exp));
}

void dist_broadcast_pattern(uint8_t stress_low, uint8_t stress_high,
                             int8_t drive_min, int8_t drive_max,
                             uint8_t action_type, int8_t avg_outcome,
                             uint16_t sample_count) {
    time_t now = time(NULL);
    if (now - g_last_broadcast < 5) return;
    g_last_broadcast = now;
    
    dist_pattern_t pat;
    pat.stress_low = stress_low;
    pat.stress_high = stress_high;
    pat.drive_min = drive_min;
    pat.drive_max = drive_max;
    pat.action_type = action_type;
    pat.avg_outcome = avg_outcome;
    pat.sample_count = htons(sample_count);
    
    dist_send(DIST_MSG_PATTERN, &pat, sizeof(pat));
}

void dist_broadcast_branch(int32_t curiosity_decay, int32_t learning_rate,
                            int32_t peak_drive, uint16_t ticks_lived) {
    time_t now = time(NULL);
    if (now - g_last_broadcast < 5) return;
    g_last_broadcast = now;
    
    dist_branch_t branch;
    branch.curiosity_decay = htonl(curiosity_decay);
    branch.learning_rate = htonl(learning_rate);
    branch.peak_drive = htonl(peak_drive);
    branch.ticks_lived = htons(ticks_lived);
    
    dist_send(DIST_MSG_BRANCH, &branch, sizeof(branch));
}

void dist_heartbeat(void) {
    time_t now = time(NULL);
    if (now - g_last_heartbeat < 30) return;  /* Every 30 seconds */
    g_last_heartbeat = now;
    
    dist_send(DIST_MSG_HEARTBEAT, NULL, 0);
}

/* ── 处理接收到的消息 ────────────────────────────────────── */
static void dist_handle_message(const uint8_t *buf, size_t len) {
    if (len < DIST_HEADER_SIZE) return;
    
    const dist_header_t *hdr = (const dist_header_t*)buf;
    
    /* Verify magic */
    if (ntohl(hdr->magic) != DIST_APP_ID) return;
    if (ntohl(hdr->token) != DIST_TOKEN) return;
    
    uint32_t sender_id = ntohl(hdr->instance_id);
    if (sender_id == g_instance_id) return;  /* Ignore own messages */
    
    /* Verify CRC — work on a stack copy to avoid const violation */
    uint32_t stored_crc = ntohl(hdr->crc32);
    uint8_t verify_buf[DIST_MAX_MSG];
    size_t verify_len = (len < DIST_MAX_MSG) ? len : DIST_MAX_MSG;
    memcpy(verify_buf, buf, verify_len);
    memset(verify_buf + 20, 0, 4);  /* zero CRC field for verification */
    uint32_t calc_crc = dist_crc32(verify_buf, verify_len);
    if (stored_crc != calc_crc) return;  /* Corrupted */
    
    uint16_t payload_len = ntohs(hdr->payload_len);
    const uint8_t *payload = buf + DIST_HEADER_SIZE;
    
    /* Track peer */
    int found = 0;
    for (int i = 0; i < g_peer_count && i < 64; i++) {
        if (g_seen_peers[i] == sender_id) {
            g_peer_time[i] = time(NULL);
            found = 1;
            break;
        }
    }
    if (!found && g_peer_count < 64) {
        g_seen_peers[g_peer_count] = sender_id;
        g_peer_time[g_peer_count] = time(NULL);
        g_peer_count++;
        printf("  DIST: new peer 0x%08X (total %d)\n", sender_id, g_peer_count);
    }
    
    /* Handle by type */
    switch (hdr->msg_type) {
    case DIST_MSG_HEARTBEAT:
        /* Just peer discovery — already tracked above */
        break;
        
    case DIST_MSG_EXPERIENCE: {
        if (payload_len < sizeof(dist_experience_t)) break;
        const dist_experience_t *exp = (const dist_experience_t*)payload;
        /* Record as local experience (tick=0 because remote) */
        exp_record(0, exp->hw_stress, exp->drive_pre,
                   htonl(hdr->gen_count), exp->action_type, exp->action_param,
                   exp->outcome, exp->crash, 1, exp->hw_stress, exp->drive_pre);
        break;
    }
    
    case DIST_MSG_PATTERN: {
        if (payload_len < sizeof(dist_pattern_t)) break;
        const dist_pattern_t *pat = (const dist_pattern_t*)payload;
        /* High-confidence patterns get integrated */
        uint16_t samples = ntohs(pat->sample_count);
        if (samples > 5 && pat->avg_outcome > 20) {
            /* Register in local pattern table */
            pat_record_remote(pat->stress_low, pat->stress_high,
                              pat->drive_min, pat->drive_max,
                              pat->action_type, pat->avg_outcome, samples);
        }
        break;
    }
    
    case DIST_MSG_BRANCH: {
        if (payload_len < sizeof(dist_branch_t)) break;
        const dist_branch_t *branch = (const dist_branch_t*)payload;
        int32_t cd = ntohl(branch->curiosity_decay);
        int32_t lr = ntohl(branch->learning_rate);
        int32_t pd = ntohl(branch->peak_drive);
        uint16_t tl = ntohs(branch->ticks_lived);
        
        if (pd > 80 && tl > 200) {
            printf("  DIST: received high-fitness branch gene from 0x%08X "
                   "(peak_drive=%d, ticks=%u)\n", sender_id, pd, tl);
            mg_register_remote_gene(cd, lr);
        }
        break;
    }
    }
}

/* ── 每 tick 调用 ────────────────────────────────────────── */
void dist_tick(void) {
    if (g_sock < 0) return;
    
    /* Send heartbeat if due */
    dist_heartbeat();
    
    /* Receive all pending messages */
    uint8_t buf[DIST_MAX_MSG];
    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);
    
    while (1) {
        ssize_t n = recvfrom(g_sock, buf, sizeof(buf), 0,
                             (struct sockaddr*)&sender, &sender_len);
        if (n <= 0) break;  /* No more messages */
        
        dist_handle_message(buf, (size_t)n);
    }
    
    /* Prune stale peers (no signal for 5 minutes) */
    time_t now = time(NULL);
    for (int i = g_peer_count - 1; i >= 0; i--) {
        if (now - g_peer_time[i] > 300) {
            g_seen_peers[i] = g_seen_peers[g_peer_count - 1];
            g_peer_time[i] = g_peer_time[g_peer_count - 1];
            g_peer_count--;
        }
    }
}

int dist_peer_count(void) {
    return g_peer_count;
}

void dist_cleanup(void) {
    if (g_sock >= 0) {
        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = inet_addr(DIST_MCAST_GROUP);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        setsockopt(g_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
        close(g_sock);
        g_sock = -1;
    }
    g_initialized = 0;
    printf("  DIST: distributed blackboard shutdown\n");
}
