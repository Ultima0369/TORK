#include "tork_mesh.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── 内部状态 ──────────────────────────────────────────── */
static t2t_mesh_state_t g_mesh;
static t2t_health_t g_self;
static int g_initialized = 0;

/* 物理层注册表 */
#define MAX_PHY 4
static struct {
    t2t_phy_t           phy;
    t2t_phy_callbacks_t cbs;
    int                 active;
} g_phys[MAX_PHY];
static int g_num_phys = 0;

/* ── 辅助: 生成随机车辆 ID ────────────────────────────── */
static void generate_id(uint8_t *out) {
    for (int i = 0; i < 8; i++) {
        out[i] = (uint8_t)(rand() & 0xFF);
    }
}

/* ── 辅助: ID 比较 ────────────────────────────────────── */
static int id_eq(const uint8_t *a, const uint8_t *b) {
    return memcmp(a, b, 8) == 0;
}

/* ── 辅助: ID 转字符串 ────────────────────────────────── */
static void id_str(const uint8_t *id, char *buf) {
    for (int i = 0; i < 8; i++) {
        sprintf(buf + i*2, "%02x", id[i]);
    }
    buf[16] = '\0';
}

/* ── 初始化 ────────────────────────────────────────────── */
void t2t_mesh_init(void) {
    memset(&g_mesh, 0, sizeof(g_mesh));
    memset(&g_self, 0, sizeof(g_self));
    memset(g_phys, 0, sizeof(g_phys));
    g_num_phys = 0;
    g_initialized = 1;

    /* 生成随机车辆 ID */
    generate_id(g_self.vehicle_id);
    g_self.magic = T2T_MAGIC;
    g_self.version = T2T_VERSION;
    g_self.confidence = 90;

    char idbuf[17];
    id_str(g_self.vehicle_id, idbuf);
    printf("  T2T MESH: init vehicle_id=%s\n", idbuf);
}

/* ── 注册物理层 ────────────────────────────────────────── */
void t2t_mesh_register_phy(t2t_phy_t phy, const t2t_phy_callbacks_t *cbs) {
    if (!g_initialized || !cbs || g_num_phys >= MAX_PHY) return;
    g_phys[g_num_phys].phy = phy;
    g_phys[g_num_phys].cbs = *cbs;
    g_phys[g_num_phys].active = 1;
    g_num_phys++;
    printf("  T2T MESH: registered phy=%d\n", phy);
}

/* ── 更新自身状态 ──────────────────────────────────────── */
void t2t_mesh_update_self(t2t_health_t *health) {
    if (!health) return;
    /* 保留车辆 ID 和 magic */
    memcpy(health->vehicle_id, g_self.vehicle_id, 8);
    health->magic = T2T_MAGIC;
    health->version = T2T_VERSION;
    health->timestamp_ms = 0;  /* 调用方填入 */
    memcpy(&g_self, health, sizeof(t2t_health_t));
}

/* ── 查找或创建对等记录 ────────────────────────────────── */
static t2t_peer_t *find_or_create_peer(const uint8_t *id) {
    /* 查找已有 */
    for (int i = 0; i < g_mesh.peer_count; i++) {
        if (id_eq(g_mesh.peers[i].vehicle_id, id)) {
            return &g_mesh.peers[i];
        }
    }

    /* 创建新记录 */
    if (g_mesh.peer_count >= 32) {
        /* 替换最旧的活跃记录 */
        uint32_t oldest_ts = UINT32_MAX;
        int oldest_idx = 0;
        for (int i = 0; i < 32; i++) {
            if (g_mesh.peers[i].stale && g_mesh.peers[i].last_seen_ms < oldest_ts) {
                oldest_ts = g_mesh.peers[i].last_seen_ms;
                oldest_idx = i;
            }
        }
        memset(&g_mesh.peers[oldest_idx], 0, sizeof(t2t_peer_t));
        memcpy(g_mesh.peers[oldest_idx].vehicle_id, id, 8);
        return &g_mesh.peers[oldest_idx];
    }

    /* 新建 */
    t2t_peer_t *p = &g_mesh.peers[g_mesh.peer_count];
    g_mesh.peer_count++;
    memset(p, 0, sizeof(t2t_peer_t));
    memcpy(p->vehicle_id, id, 8);
    return p;
}

/* ── 收到对等车辆健康广播 ──────────────────────────────── */
void t2t_mesh_on_health(const uint8_t *vehicle_id,
                         const t2t_health_t *health,
                         t2t_phy_t phy, uint8_t signal) {
    if (!g_initialized || !vehicle_id || !health) return;
    if (health->magic != T2T_MAGIC) return;

    /* 忽略自己 */
    if (id_eq(vehicle_id, g_self.vehicle_id)) return;

    t2t_peer_t *peer = find_or_create_peer(vehicle_id);
    if (!peer) return;

    peer->last_health = *health;
    peer->last_seen_ms = health->timestamp_ms;
    if (peer->first_seen_ms == 0) {
        peer->first_seen_ms = health->timestamp_ms;
    }
    peer->signal_strength = signal;
    peer->encounter_count++;
    peer->stale = 0;

    /* 抽稀打印: 每 5 次相遇才打印 */
    if (peer->encounter_count % 5 == 1) {
        char idbuf[17];
        id_str(vehicle_id, idbuf);
        printf("  T2T MESH: peer %s risk=%d brake=%d%% battery=%d%%\n",
               idbuf, health->overall_risk, health->brake_health, health->battery_soc);
    }

    g_mesh.total_encounters++;

    /* 触发风险重新评估 */
    uint8_t risk;
    int dist = t2t_mesh_assess_risk(vehicle_id, &risk);
    peer->risk_to_me = risk;
    if (dist > 0) peer->distance_m = dist;
}

/* ── 风险评估 ──────────────────────────────────────────── */
int t2t_mesh_assess_risk(const uint8_t *vehicle_id, uint8_t *risk_out) {
    if (!g_initialized || !vehicle_id) return -1;

    for (int i = 0; i < g_mesh.peer_count; i++) {
        if (!id_eq(g_mesh.peers[i].vehicle_id, vehicle_id)) continue;

        t2t_health_t *h = &g_mesh.peers[i].last_health;
        uint32_t risk = 0;

        /* 刹车健康 (权重最大) */
        if (h->brake_health < 30) risk += 40;
        else if (h->brake_health < 50) risk += 25;
        else if (h->brake_health < 70) risk += 10;

        /* 刹车片磨损 */
        if (h->brake_pad_wear > 80) risk += 15;
        else if (h->brake_pad_wear > 60) risk += 8;

        /* 热衰减风险 */
        risk += h->brake_fade_risk / 2;

        /* 轮胎 */
        if (h->tire_wear > 80) risk += 10;
        if (h->tire_pressure[0] > 0) {
            int low_pressure = 0;
            for (int t = 0; t < 4; t++) {
                if (h->tire_pressure[t] > 0 && h->tire_pressure[t] < 20) low_pressure++;
            }
            risk += low_pressure * 5;
        }

        /* 电池异常 */
        if (h->battery_temp_delta > 10) risk += 15;
        if (h->battery_health < 40) risk += 10;

        /* 驾驶行为 */
        risk += h->hard_brake_count * 3;
        risk += h->hard_accel_count * 2;
        risk += h->lane_deviation_count * 3;

        /* 综合风险 */
        if (risk > 100) risk = 100;
        if (risk_out) *risk_out = (uint8_t)risk;

        /* 建议距离: 风险越高, 距离越远 */
        if (risk < 20) return 0;        /* 安全 */
        if (risk < 40) return 30;       /* 30 米 */
        if (risk < 60) return 50;       /* 50 米 */
        if (risk < 80) return 100;      /* 100 米 */
        return 200;                      /* 200 米以上 */
    }

    return -1;
}

/* ── 最危险车辆 ────────────────────────────────────────── */
const uint8_t *t2t_mesh_most_dangerous(uint8_t *risk_out) {
    if (!g_initialized) return NULL;

    uint8_t max_risk = 0;
    const uint8_t *max_id = NULL;

    for (int i = 0; i < g_mesh.peer_count; i++) {
        if (g_mesh.peers[i].stale) continue;
        uint8_t risk = 0;
        t2t_mesh_assess_risk(g_mesh.peers[i].vehicle_id, &risk);
        if (risk > max_risk) {
            max_risk = risk;
            max_id = g_mesh.peers[i].vehicle_id;
        }
    }

    if (risk_out) *risk_out = max_risk;
    return max_id;
}

/* ── 建议 ──────────────────────────────────────────────── */
const char *t2t_mesh_advice_str(const uint8_t *vehicle_id, t2t_advice_t *advice_out) {
    if (!g_initialized || !vehicle_id) {
        if (advice_out) *advice_out = ADVICE_NONE;
        return "unknown";
    }

    uint8_t risk = 0;
    t2t_mesh_assess_risk(vehicle_id, &risk);

    t2t_advice_t adv;
    const char *str;

    if (risk >= 80) {
        adv = ADVICE_EMERGENCY;
        str = "⚠️ 紧急避让: 该车风险极高, 立即远离!";
    } else if (risk >= 60) {
        adv = ADVICE_ALERT;
        str = "⚠️ 注意: 该车刹车/轮胎状态差, 保持100米以上";
    } else if (risk >= 40) {
        adv = ADVICE_CAUTION;
        str = "⚡ 谨慎: 该车车况一般, 建议保持50米";
    } else if (risk >= 20) {
        adv = ADVICE_KEEP_DIST;
        str = "→ 留意: 该车存在轻微异常, 正常跟车即可";
    } else {
        adv = ADVICE_NONE;
        str = "✓ 安全: 该车状态良好";
    }

    if (advice_out) *advice_out = adv;
    return str;
}

/* ── 获取网格状态 ──────────────────────────────────────── */
const t2t_mesh_state_t *t2t_mesh_get_state(void) {
    return g_initialized ? &g_mesh : NULL;
}

/* ── 广播自身健康状态 ──────────────────────────────────── */
void t2t_mesh_broadcast(void) {
    if (!g_initialized) return;

    g_self.timestamp_ms = 0;

    for (int i = 0; i < g_num_phys; i++) {
        if (g_phys[i].active && g_phys[i].cbs.send_health) {
            g_phys[i].cbs.send_health(&g_self);
        }
    }
}


/* ── 静态密钥 ──────────────────────────────────────────── */
static tork_hmac_ctx_t g_hmac;
static int g_key_set = 0;
static uint64_t g_nonce_counter = 0;

/* ── 设置 HMAC 密钥 ────────────────────────────────────── */
void t2t_mesh_set_key(const uint8_t *key, size_t key_len) {
    tork_hmac_init(&g_hmac, key, key_len);
    g_key_set = 1;
    printf("  T2T MESH: HMAC key set (%zu bytes)\n", key_len);
}

/* ── 广播前签名 ────────────────────────────────────────── */
void t2t_mesh_broadcast_signed(void) {
    if (!g_initialized || !g_key_set) {
        t2t_mesh_broadcast();
        return;
    }

    /* 填充随机数和签名位置 */
    g_nonce_counter++;
    for (int i = 0; i < 8; i++)
        g_self.nonce[i] = (uint8_t)(g_nonce_counter >> (i * 8));
    memset(g_self.signature, 0, 32);  /* 签名前清空 */

    /* 签名: HMAC over whole health struct */
    tork_hmac_sign(&g_hmac, (const uint8_t *)&g_self, sizeof(t2t_health_t),
                   g_self.signature);

    g_self.timestamp_ms = 0;

    for (int i = 0; i < g_num_phys; i++) {
        if (g_phys[i].active && g_phys[i].cbs.send_health) {
            g_phys[i].cbs.send_health(&g_self);
        }
    }
}

/* ── 接收并验证签名广播 ────────────────────────────────── */
int t2t_mesh_on_health_signed(const uint8_t *vehicle_id,
                               const t2t_health_t *health,
                               t2t_phy_t phy, uint8_t signal) {
    if (!g_initialized || !vehicle_id || !health) return 0;
    if (health->magic != T2T_MAGIC) return 0;
    if (id_eq(vehicle_id, g_self.vehicle_id)) return 0;

    /* 如果已设置密钥, 验证签名 */
    if (g_key_set) {
        t2t_health_t copy = *health;
        uint8_t expected_sig[32];
        memcpy(expected_sig, copy.signature, 32);
        memset(copy.signature, 0, 32);

        uint8_t computed_sig[32];
        tork_hmac_sign(&g_hmac, (const uint8_t *)&copy, sizeof(t2t_health_t),
                       computed_sig);

        if (memcmp(expected_sig, computed_sig, 32) != 0) {
            char idbuf[17];
            id_str(vehicle_id, idbuf);
            printf("  T2T MESH: ⚠️ SIGNATURE MISMATCH from %s — rejected\n", idbuf);
            return 0;
        }
    }

    /* 签名验证通过, 正常处理 */
    t2t_mesh_on_health(vehicle_id, health, phy, signal);
    return 1;
}
