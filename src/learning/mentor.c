/* ── 师徒阶段管理器 ──────────────────────────────────────────
 *  TORK 的成长轨迹：学徒 → 有主见 → 超越师父
 *  阶段判定基于经验数量、模式置信度、TLN 一致性
 * ─────────────────────────────────────────────────────────── */

#include "mentor.h"
#include "../config.h"
#include "self_tune.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define PERSIST_PATH TORK_MENTOR_PATH

static mentor_state_t g_mentor = {0};

/* ── 阶段阈值 ── */
static const struct {
    uint32_t exp_min;
    float    conf_min;
    float    tln_cons_min;
} stage_thresholds[] = {
    [MENTOR_APPRENTICE]  = { 0,    0.0f, 0.0f },
    [MENTOR_OPINIONATED] = { 500,  0.5f, 0.4f },
    [MENTOR_TRANSCEND]   = { 2000, 0.8f, 0.7f },
};

void mentor_init(void) {
    memset(&g_mentor, 0, sizeof(g_mentor));
    g_mentor.stage = MENTOR_APPRENTICE;
    g_mentor.cloud_weight = 80;
    g_mentor.local_weight = 15;
    g_mentor.autonomous_weight = 5;
    mentor_load();
}

/* ── 阶段判定 ── */
static mentor_stage_t determine_stage(uint32_t exp, float conf, float tln_cons) {
    if (exp >= stage_thresholds[MENTOR_TRANSCEND].exp_min &&
        conf >= stage_thresholds[MENTOR_TRANSCEND].conf_min &&
        tln_cons >= stage_thresholds[MENTOR_TRANSCEND].tln_cons_min) {
        return MENTOR_TRANSCEND;
    }
    if (exp >= stage_thresholds[MENTOR_OPINIONATED].exp_min &&
        conf >= stage_thresholds[MENTOR_OPINIONATED].conf_min &&
        tln_cons >= stage_thresholds[MENTOR_OPINIONATED].tln_cons_min) {
        return MENTOR_OPINIONATED;
    }
    return MENTOR_APPRENTICE;
}

/* ── 权重计算 ── */
static void recalc_weights(void) {
    switch (g_mentor.stage) {
    case MENTOR_APPRENTICE:
        g_mentor.cloud_weight = 80;
        g_mentor.local_weight = 15;
        g_mentor.autonomous_weight = 5;
        break;
    case MENTOR_OPINIONATED:
        g_mentor.cloud_weight = 20;
        g_mentor.local_weight = 65;
        g_mentor.autonomous_weight = 15;
        break;
    case MENTOR_TRANSCEND:
        g_mentor.cloud_weight = 5;
        g_mentor.local_weight = 30;
        g_mentor.autonomous_weight = 65;
        break;
    }
}

void mentor_tick(uint32_t exp_count, float pattern_conf, float tln_consistency) {
    g_mentor.pattern_confidence = pattern_conf;
    g_mentor.tln_consistency = tln_consistency;

    mentor_stage_t new_stage = determine_stage(exp_count, pattern_conf, tln_consistency);

    if (new_stage != g_mentor.stage) {
        fprintf(stderr, "mentor: stage %s → %s (exp=%u conf=%.2f tln=%.2f)\n",
                mentor_stage_name(g_mentor.stage),
                mentor_stage_name(new_stage),
                exp_count, pattern_conf, tln_consistency);
        g_mentor.stage = new_stage;
        recalc_weights();
        mentor_save();

        /* 阶段转换影响 self_tune 参数 */
        if (new_stage == MENTOR_OPINIONATED) {
            /* 有主见：增探索，增学习 */
            tune_set_param("exploration_rate", 35);
            tune_set_param("learning_rate", 0.15f);
        } else if (new_stage == MENTOR_TRANSCEND) {
            /* 超越：高自主，高好奇心 */
            tune_set_param("curiosity_weight", 1.5f);
            tune_set_param("desire_weight", 1.2f);
        }
    }
}

mentor_stage_t mentor_get_stage(void) {
    return g_mentor.stage;
}

const mentor_state_t *mentor_get_state(void) {
    return &g_mentor;
}

void mentor_decision_weights(uint8_t *cloud, uint8_t *local, uint8_t *autonomous) {
    *cloud = g_mentor.cloud_weight;
    *local = g_mentor.local_weight;
    *autonomous = g_mentor.autonomous_weight;
}

void mentor_record_cloud_query(void) {
    g_mentor.cloud_queries++;
}

void mentor_record_local_decision(void) {
    g_mentor.local_decisions++;
}

void mentor_record_autonomous_mutation(void) {
    g_mentor.autonomous_mutations++;
}

const char *mentor_stage_name(mentor_stage_t stage) {
    switch (stage) {
    case MENTOR_APPRENTICE:  return "学徒";
    case MENTOR_OPINIONATED: return "有主见";
    case MENTOR_TRANSCEND:   return "超越";
    }
    return "未知";
}

/* ── 持久化 ── */
void mentor_save(void) {
    FILE *f = fopen(PERSIST_PATH, "wb");
    if (!f) return;
    if (fwrite(&g_mentor, sizeof(g_mentor), 1, f) != 1) {
        fclose(f);
        return;
    }
    fclose(f);
}

void mentor_load(void) {
    FILE *f = fopen(PERSIST_PATH, "rb");
    if (!f) return;
    if (fread(&g_mentor, sizeof(g_mentor), 1, f) != 1) {
        fclose(f);
        memset(&g_mentor, 0, sizeof(g_mentor));
        g_mentor.stage = MENTOR_APPRENTICE;
        return;
    }
    fclose(f);
    if (g_mentor.stage > MENTOR_TRANSCEND) g_mentor.stage = MENTOR_APPRENTICE;
    recalc_weights();
}
