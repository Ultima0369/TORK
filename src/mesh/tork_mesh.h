#ifndef TORK_MESH_H
#define TORK_MESH_H

/* ══════════════════════════════════════════════════════════════
 * TORK 车际网格协议 (T2T Mesh)
 *
 * 让装了 TORK 的设备在物理临近时自动发现、握手、交换健康状态。
 * 不依赖云，不依赖 5G，只靠物理接触（USB 热插拔 / WiFi-Direct / BLE）。
 *
 * 核心信条：
 *   "每台车都知道自己的状态，每台车都能告诉别人自己的状态，
 *    每台车都能听到别人的状态并做出判断。"
 * ══════════════════════════════════════════════════════════════ */

#include <stdint.h>
#include "../config.h"
#include "../crypto/tork_sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── T2T 协议版本 ─────────────────────────────────────── */
#define T2T_MAGIC          0x54325454  /* "T2TT" */
#define T2T_VERSION        0x0002      /* v0.2 — signed */

/* ── 车辆健康状态 (每台 TORK 定期广播) ──────────────────── */
typedef struct {
    uint32_t magic;                /* T2T_MAGIC */
    uint16_t version;              /* T2T_VERSION */

    /* 车辆身份 (匿名, 无 VIN/车牌) */
    uint8_t  vehicle_id[8];        /* 哈希标识, 每次上电随机生成 */
    uint8_t  vehicle_type;         /* 0=未知 1=轿车 2=SUV 3=货车 4=电动 5=混动 */

    /* 刹车系统 (0-100) */
    uint8_t  brake_health;         /* 刹车健康度, 100=全新 */
    uint8_t  brake_pad_wear;       /* 刹车片磨损, 0=新 100=极限 */
    uint8_t  brake_fade_risk;      /* 热衰减风险, 0=低 100=高 */

    /* 轮胎 */
    uint8_t  tire_pressure[4];     /* 四轮胎压 (kPa/10), 0=未知 */
    uint8_t  tire_wear;            /* 轮胎磨损, 0=新 100=极限 */

    /* 电池/动力 */
    uint8_t  battery_soc;          /* 电量 (%), 油车=100 */
    uint8_t  battery_health;       /* 电池健康度, 100=全新 */
    int8_t   battery_temp_delta;   /* 温度偏离正常 (+°C) */

    /* 行为 (最近 5 分钟统计) */
    uint8_t  hard_brake_count;     /* 急刹车次数 */
    uint8_t  hard_accel_count;     /* 急加速次数 */
    uint8_t  lane_deviation_count; /* 偏离车道次数 */
    uint8_t  speed_avg;            /* 平均速度 (km/h) */

    /* 综合 */
    uint8_t  overall_risk;         /* 综合风险 0-100 (0=好 100=危险) */
    uint8_t  confidence;           /* 自评估置信度 0-100 */

    /* 元数据 */
    /* 元数据 */
    uint32_t timestamp_ms;         /* 时间戳 */
    uint8_t  generation;           /* TORK 代数 */
    uint8_t  nonce[8];             /* 防重放随机数 */
    uint8_t  signature[32];        /* HMAC-SHA256 签名 (覆盖前面所有字段) */
} __attribute__((packed)) t2t_health_t;

/* ── 对等车辆记录 ────────────────────────────────────── */
typedef struct {
    uint8_t      vehicle_id[8];
    t2t_health_t last_health;
    uint32_t     first_seen_ms;
    uint32_t     last_seen_ms;
    uint8_t      signal_strength;   /* 0-100 */
    uint8_t      encounter_count;   /* 相遇次数 */
    int8_t       distance_m;        /* 估计距离 (米, -1=未知) */
    uint8_t      risk_to_me;        /* 对"我"的风险 0-100 */
    uint8_t      stale;             /* 1=已离线 */
    uint8_t      key[32];            /* 对等车辆的公钥/共享密钥 */
    uint8_t      key_verified;       /* 1=已通过带外交互验证 */
} t2t_peer_t;

/* ── 网格状态 ────────────────────────────────────────── */
typedef struct {
    t2t_peer_t  peers[32];          /* 最多跟踪 32 台车 */
    uint8_t     peer_count;
    uint8_t     my_risk_level;       /* 自身风险等级 0-100 */
    uint32_t    total_encounters;
    uint32_t    alerts_issued;       /* 发出的提醒次数 */
} t2t_mesh_state_t;

/* ── 物理传输类型 ─────────────────────────────────────── */
typedef enum {
    PHY_NONE    = 0,
    PHY_USB     = 1,   /* USB 热插拔 (携带配置/日志) */
    PHY_WIFI    = 2,   /* WiFi-Direct / ad-hoc */
    PHY_BLE     = 3,   /* 蓝牙低功耗广播 */
    PHY_LORA    = 4,   /* LoRa 长距离 */
    PHY_CAN     = 5,   /* 车载 CAN bus (同车多 TORK) */
} t2t_phy_t;

/* ── 物理传输回调 ─────────────────────────────────────── */
typedef struct {
    void (*on_peer_discovered)(const uint8_t *id, t2t_phy_t phy, uint8_t signal);
    void (*on_health_received)(const uint8_t *id, const t2t_health_t *health);
    void (*on_peer_lost)(const uint8_t *id);
    int   (*send_health)(const t2t_health_t *health);  /* 返回 0=ok */
} t2t_phy_callbacks_t;

/* ── 公共 API ──────────────────────────────────────────── */

/* 初始化 T2T 网格 */
void t2t_mesh_init(void);

/* 注册物理传输层 (可多次调用, 支持多通道) */
void t2t_mesh_register_phy(t2t_phy_t phy, const t2t_phy_callbacks_t *cbs);

/* 更新自身健康状态 (每次 TORK tick 调用)
 * 由 TORK 的边缘传感器填充 health 字段 */
void t2t_mesh_update_self(t2t_health_t *health);

/* 收到对等车辆的健康广播后调用 */
void t2t_mesh_on_health(const uint8_t *vehicle_id, const t2t_health_t *health, t2t_phy_t phy, uint8_t signal);

/* 评估对"我"的风险: 返回需要保持的距离 (米) */
int t2t_mesh_assess_risk(const uint8_t *vehicle_id, uint8_t *risk_out);

/* 获取最危险的对等车辆 ID (0=无) */
const uint8_t *t2t_mesh_most_dangerous(uint8_t *risk_out);

/* 建议: 对某台车应该怎么做 */
typedef enum {
    ADVICE_NONE       = 0,
    ADVICE_KEEP_DIST  = 1,   /* 保持距离 */
    ADVICE_CAUTION    = 2,   /* 谨慎 */
    ADVICE_ALERT      = 3,   /* 立即远离 */
    ADVICE_EMERGENCY  = 4,   /* 紧急避让 */
} t2t_advice_t;
const char *t2t_mesh_advice_str(const uint8_t *vehicle_id, t2t_advice_t *advice_out);

/* 获取网格快照 */
const t2t_mesh_state_t *t2t_mesh_get_state(void);

/* ── 认证 API ──────────────────────────────────────────── */

/* 设置本车 HMAC 密钥 (启动时调用一次) */
void t2t_mesh_set_key(const uint8_t *key, size_t key_len);

/* 安全广播: 签名后广播 */
void t2t_mesh_broadcast_signed(void);

/* 接收并验证签名广播: 返回 1=验证通过, 0=失败 */
int t2t_mesh_on_health_signed(const uint8_t *vehicle_id,
                               const t2t_health_t *health,
                               t2t_phy_t phy, uint8_t signal);

/* 广播自身健康状态到所有已注册的物理层 */
void t2t_mesh_broadcast(void);

#ifdef __cplusplus
}
#endif

#endif /* TORK_MESH_H */
