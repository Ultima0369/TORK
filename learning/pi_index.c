#include "pi_index.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ── 全局状态 ─────────────────────────────────────────────── */
static pi_index_t g_index;
static int g_idx_initialized = 0;

/* ── 初始化 ──────────────────────────────────────────────── */
void pidx_init(void) {
    memset(&g_index, 0, sizeof(g_index));
    g_idx_initialized = 1;
    printf("  PIDX: pi-index initialized (%d slots)\n", PI_INDEX_SLOTS);
}

/* ── 索引一条新记忆 ──────────────────────────────────────── */
int pidx_add(const pi_profile_t *profile, uint32_t ref_id, uint8_t category) {
    if (!g_idx_initialized || !profile) return -1;

    /* 先检查是否已有相似指纹（避免重复索引同类振动） */
    pi_match_t existing = pidx_query(profile, 0.9f);
    if (existing.slot_idx >= 0) {
        /* 同类振动已存在，更新热度而非重复添加 */
        pi_index_entry_t *e = &g_index.slots[existing.slot_idx];
        e->match_count++;
        e->ref_id = ref_id;  /* 更新为最新的关联 ID */
        return existing.slot_idx;
    }

    /* 找空闲槽位 */
    for (int i = 0; i < PI_INDEX_SLOTS; i++) {
        if (!g_index.slots[i].active) {
            pi_index_entry_t *e = &g_index.slots[i];
            e->profile = *profile;
            e->ref_id = ref_id;
            e->category = category;
            e->active = 1;
            e->match_count = 1;
            e->last_match_tick = g_index.learn_tick;
            g_index.total_entries++;
            return i;
        }
    }

    /* 全满，替换最冷门的（匹配次数最少 + 最久未匹配） */
    int coldest = 0;
    float coldest_score = 999999.0f;
    for (int i = 0; i < PI_INDEX_SLOTS; i++) {
        pi_index_entry_t *e = &g_index.slots[i];
        float score = (float)e->match_count /
                      (1.0f + (float)(g_index.learn_tick - e->last_match_tick));
        if (score < coldest_score) {
            coldest_score = score;
            coldest = i;
        }
    }

    pi_index_entry_t *e = &g_index.slots[coldest];
    e->profile = *profile;
    e->ref_id = ref_id;
    e->category = category;
    e->active = 1;
    e->match_count = 1;
    e->last_match_tick = g_index.learn_tick;
    return coldest;
}

/* ── 在 π 空间查找最相似的振动 ────────────────────────────── */
pi_match_t pidx_query(const pi_profile_t *profile, float threshold) {
    pi_match_t no_match = {-1, 0.0f, 0, 0};
    if (!g_idx_initialized || !profile) return no_match;

    int best_idx = -1;
    float best_sim = 0.0f;

    for (int i = 0; i < PI_INDEX_SLOTS; i++) {
        pi_index_entry_t *e = &g_index.slots[i];
        if (!e->active) continue;

        float sim = pi_profile_similarity(profile, &e->profile);

        /* 时效性：旧指纹的相似度打折
         * 世界变了，旧识别必须被新经验刷新 */
        uint32_t age = (g_index.learn_tick > e->last_match_tick) ?
                       (g_index.learn_tick - e->last_match_tick) : 0;
        float decayed_sim = pi_decay(sim, age, 500);

        if (decayed_sim > best_sim) {
            best_sim = decayed_sim;
            best_idx = i;
        }
    }

    if (best_idx >= 0 && best_sim >= threshold) {
        pi_match_t m;
        m.slot_idx = best_idx;
        m.similarity = best_sim;
        m.ref_id = g_index.slots[best_idx].ref_id;
        m.category = g_index.slots[best_idx].category;
        return m;
    }

    return no_match;
}

/* ── 批量查询 top-K ─────────────────────────────────────── */
int pidx_query_top_k(const pi_profile_t *profile, float threshold,
                     int k, pi_match_t *results) {
    if (!g_idx_initialized || !profile || !results || k <= 0) return 0;

    int found = 0;

    for (int i = 0; i < PI_INDEX_SLOTS && found < k; i++) {
        pi_index_entry_t *e = &g_index.slots[i];
        if (!e->active) continue;

        float sim = pi_profile_similarity(profile, &e->profile);
        uint32_t age = (g_index.learn_tick > e->last_match_tick) ?
                       (g_index.learn_tick - e->last_match_tick) : 0;
        float decayed_sim = pi_decay(sim, age, 500);

        if (decayed_sim < threshold) continue;

        /* 插入排序到 top-K */
        int pos = found;
        for (int j = 0; j < found; j++) {
            if (decayed_sim > results[j].similarity) {
                pos = j;
                break;
            }
        }
        if (found < k) {
            /* 后移 */
            for (int j = found; j > pos; j--)
                results[j] = results[j-1];
            results[pos].slot_idx = i;
            results[pos].similarity = decayed_sim;
            results[pos].ref_id = e->ref_id;
            results[pos].category = e->category;
            found++;
        } else if (pos < k) {
            for (int j = k - 1; j > pos; j--)
                results[j] = results[j-1];
            results[pos].slot_idx = i;
            results[pos].similarity = decayed_sim;
            results[pos].ref_id = e->ref_id;
            results[pos].category = e->category;
        }
    }

    return found;
}

/* ── 用原始观测序列直接查询 ──────────────────────────────── */
pi_match_t pidx_query_raw(const uint8_t *buf, int len, float threshold) {
    pi_profile_t profile = pi_hash_profile(buf, len);
    return pidx_query(&profile, threshold);
}

/* ── 更新匹配热度 ────────────────────────────────────────── */
void pidx_touch(int slot_idx, uint32_t tick) {
    if (!g_idx_initialized || slot_idx < 0 || slot_idx >= PI_INDEX_SLOTS) return;
    pi_index_entry_t *e = &g_index.slots[slot_idx];
    if (!e->active) return;
    e->match_count++;
    e->last_match_tick = tick;
}

/* ── 获取条目数 ──────────────────────────────────────────── */
int pidx_count(void) {
    if (!g_idx_initialized) return 0;
    return (int)g_index.total_entries;
}

/* ── 持久化 ──────────────────────────────────────────────── */
int pidx_save(void) {
    if (!g_idx_initialized) return -1;

    FILE *f = fopen(PI_INDEX_PATH, "wb");
    if (!f) {
        printf("  PIDX: cannot save to %s\n", PI_INDEX_PATH);
        return -1;
    }

    uint32_t count = 0;
    for (int i = 0; i < PI_INDEX_SLOTS; i++)
        if (g_index.slots[i].active) count++;

    fwrite(&count, sizeof(count), 1, f);
    fwrite(&g_index.learn_tick, sizeof(g_index.learn_tick), 1, f);

    for (int i = 0; i < PI_INDEX_SLOTS; i++) {
        if (g_index.slots[i].active)
            fwrite(&g_index.slots[i], sizeof(pi_index_entry_t), 1, f);
    }

    fclose(f);
    printf("  PIDX: saved %u profiles to %s\n", count, PI_INDEX_PATH);
    return 0;
}

int pidx_load(void) {
    if (!g_idx_initialized) pidx_init();

    FILE *f = fopen(PI_INDEX_PATH, "rb");
    if (!f) {
        printf("  PIDX: no saved index at %s (fresh start)\n", PI_INDEX_PATH);
        return -1;
    }

    uint32_t count;
    fread(&count, sizeof(count), 1, f);
    fread(&g_index.learn_tick, sizeof(g_index.learn_tick), 1, f);

    if (count > PI_INDEX_SLOTS) count = PI_INDEX_SLOTS;

    memset(g_index.slots, 0, sizeof(g_index.slots));
    for (uint32_t i = 0; i < count; i++) {
        pi_index_entry_t e;
        if (fread(&e, sizeof(pi_index_entry_t), 1, f) != 1) break;
        if (i < PI_INDEX_SLOTS) {
            g_index.slots[i] = e;
            g_index.total_entries++;
        }
    }

    fclose(f);
    printf("  PIDX: loaded %u profiles from %s (tick=%u)\n",
           count, PI_INDEX_PATH, g_index.learn_tick);
    return 0;
}

/* ── 清理 ────────────────────────────────────────────────── */
void pidx_cleanup(void) {
    memset(&g_index, 0, sizeof(g_index));
    g_idx_initialized = 0;
    printf("  PIDX: pi-index cleaned up\n");
}
