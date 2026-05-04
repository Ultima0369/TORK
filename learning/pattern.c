#include "pattern.h"
#include "experience.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ── 全局状态 ─────────────────────────────────────────────── */
static pattern_learner_t g_learner;
static int g_initialized = 0;

/* ── 工具：将 drive 量化为 8 档 (-4..3) ────────────────────── */
static int8_t quantize_drive(int8_t drive) {
    if (drive < -80) return -4;
    if (drive < -50) return -3;
    if (drive < -20) return -2;
    if (drive < 0)   return -1;
    if (drive < 20)  return 0;
    if (drive < 50)  return 1;
    if (drive < 80)  return 2;
    return 3;
}

/* ── 工具：将世代量化为 4 档 ────────────────────────────────── */
static uint8_t quantize_gen(uint16_t gen) {
    if (gen < 5)   return 0;
    if (gen < 20)  return 1;
    if (gen < 100) return 2;
    return 3;
}

/* ── 工具：从经验构建模式键 ─────────────────────────────────── */
static pattern_key_t make_key(const experience_t *exp) {
    pattern_key_t key;
    key.hw_stress    = exp->hw_stress;
    key.drive_bucket = quantize_drive(exp->drive_pre);
    key.gen_bucket   = quantize_gen(exp->gen_count);
    key.action_type  = exp->action_type;
    return key;
}

/* ── 工具：查找或创建模式槽位 ───────────────────────────────── */
static pattern_t *find_or_create_slot(pattern_key_t key) {
    /* 先查找已有匹配 */
    for (int i = 0; i < PATTERN_MAX_SLOTS; i++) {
        if (!g_learner.slots[i].active) continue;
        pattern_t *p = &g_learner.slots[i];
        if (p->key.hw_stress    == key.hw_stress &&
            p->key.drive_bucket == key.drive_bucket &&
            p->key.gen_bucket   == key.gen_bucket &&
            p->key.action_type  == key.action_type) {
            return p;
        }
    }
    
    /* 没找到，找空闲槽位创建新模式 */
    for (int i = 0; i < PATTERN_MAX_SLOTS; i++) {
        if (!g_learner.slots[i].active) {
            pattern_t *p = &g_learner.slots[i];
            memset(p, 0, sizeof(pattern_t));
            p->key = key;
            p->active = 1;
            g_learner.total_patterns++;
            return p;
        }
    }
    
    /* 全满，覆盖最久未使用的 */
    int oldest = 0;
    uint32_t oldest_tick = 0xFFFFFFFF;
    for (int i = 0; i < PATTERN_MAX_SLOTS; i++) {
        if (g_learner.slots[i].last_seen_tick < oldest_tick) {
            oldest_tick = g_learner.slots[i].last_seen_tick;
            oldest = i;
        }
    }
    pattern_t *p = &g_learner.slots[oldest];
    memset(p, 0, sizeof(pattern_t));
    p->key = key;
    p->active = 1;
    return p;
}

/* ── 初始化 ───────────────────────────────────────────────── */
void pat_init(void) {
    memset(&g_learner, 0, sizeof(g_learner));
    g_initialized = 1;
    printf("  PAT: pattern learner initialized (%d slots)\n", PATTERN_MAX_SLOTS);
}

/* ── 从经验缓冲区学习 ──────────────────────────────────────── */
void pat_learn_from_experience(void) {
    if (!g_initialized) return;
    
    int recent_count = exp_count();
    if (recent_count < PATTERN_MIN_SAMPLES) return;
    
    g_learner.learn_cycles++;
    
    /* 读取最近的 N 条经验 (上限 200) */
    int n = (recent_count < 200) ? recent_count : 200;
    experience_t batch[200];
    int actual = exp_recent(n, batch);
    if (actual < PATTERN_MIN_SAMPLES) return;
    
    int new_patterns = 0;
    
    for (int i = 0; i < actual; i++) {
        experience_t *e = &batch[i];
        
        /* 跳过没有 outcome 的经验（还没回填的） */
        if (e->outcome == 0 && !e->crash_occurred) continue;
        
        pattern_key_t key = make_key(e);
        pattern_t *slot = find_or_create_slot(key);
        
        /* 更新统计 */
        slot->total_outcome += e->outcome;
        if (e->crash_occurred) slot->total_crashes++;
        slot->sample_count++;
        slot->last_seen_tick = (uint32_t)(e->tick & 0xFFFFFFFF);
        slot->avg_outcome = (float)slot->total_outcome / slot->sample_count;
        slot->crash_rate = (float)slot->total_crashes / slot->sample_count;
        
        if (slot->sample_count == 1) new_patterns++;
    }
    
    /* 只在有新模式或每 10 轮打印一次 */
    if (new_patterns > 0) {
        printf("  PAT: learned %d new patterns (total=%d, cycle=%u)\n",
               new_patterns, g_learner.total_patterns, g_learner.learn_cycles);
    }
}

/* ── 查询最佳行动 ──────────────────────────────────────────── */
int pat_query_best_action(uint8_t hw_stress, int8_t drive,
                          uint16_t gen_count, float *confidence) {
    if (!g_initialized) return -1;
    
    uint8_t db = quantize_drive(drive);
    uint8_t gb = quantize_gen(gen_count);
    
    int best_action = -1;
    float best_score = -9999.0f;
    float best_samples = 0;
    
    for (int i = 0; i < PATTERN_MAX_SLOTS; i++) {
        pattern_t *p = &g_learner.slots[i];
        if (!p->active) continue;
        if (p->key.hw_stress != hw_stress) continue;
        if (p->key.drive_bucket != db) continue;
        if (p->key.gen_bucket != gb) continue;
        if (p->sample_count < PATTERN_MIN_SAMPLES) continue;
        
        /* 综合评分：avg_outcome * sqrt(samples) 以平衡置信度和样本量 */
        float score = p->avg_outcome * sqrtf((float)p->sample_count);
        
        /* 惩罚高崩溃率 */
        score *= (1.0f - p->crash_rate * 0.5f);
        
        if (score > best_score) {
            best_score = score;
            best_action = p->key.action_type;
            best_samples = (float)p->sample_count;
        }
    }
    
    if (confidence) {
        *confidence = (best_action >= 0) ? 
            (best_samples / (best_samples + 5.0f)) : 0.0f;
    }
    
    return best_action;
}

/* ── 预测某个行动的期望 outcome ────────────────────────────── */
float pat_predict_outcome(uint8_t hw_stress, int8_t drive,
                          uint16_t gen_count, uint8_t action_type) {
    if (!g_initialized) return 0.0f;
    
    uint8_t db = quantize_drive(drive);
    uint8_t gb = quantize_gen(gen_count);
    
    for (int i = 0; i < PATTERN_MAX_SLOTS; i++) {
        pattern_t *p = &g_learner.slots[i];
        if (!p->active) continue;
        if (p->key.hw_stress   != hw_stress) continue;
        if (p->key.drive_bucket != db) continue;
        if (p->key.gen_bucket  != gb) continue;
        if (p->key.action_type != action_type) continue;
        if (p->sample_count < PATTERN_MIN_SAMPLES) return 0.0f;
        
        return p->avg_outcome;
    }
    
    return 0.0f;  /* 未知情境 */
}

/* ── 获取已学习的模式数 ────────────────────────────────────── */
int pat_count(void) {
    if (!g_initialized) return 0;
    return g_learner.total_patterns;
}

/* ── 获取学习轮数 ──────────────────────────────────────────── */
uint32_t pat_cycles(void) {
    if (!g_initialized) return 0;
    return g_learner.learn_cycles;
}

/* ── 保存模式库 ────────────────────────────────────────────── */
int pat_save(void) {
    if (!g_initialized) return -1;
    
    const char *path = "persist/patterns.bin";
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("  PAT: cannot save to %s\n", path);
        return -1;
    }
    
    /* 写入模式计数和所有活跃模式 */
    uint32_t count = 0;
    for (int i = 0; i < PATTERN_MAX_SLOTS; i++) {
        if (g_learner.slots[i].active) count++;
    }
    
    fwrite(&count, sizeof(count), 1, f);
    fwrite(&g_learner.learn_cycles, sizeof(g_learner.learn_cycles), 1, f);
    
    for (int i = 0; i < PATTERN_MAX_SLOTS; i++) {
        if (g_learner.slots[i].active) {
            fwrite(&g_learner.slots[i], sizeof(pattern_t), 1, f);
        }
    }
    
    fclose(f);
    printf("  PAT: saved %u patterns to %s\n", count, path);
    return 0;
}

/* ── 加载模式库 ────────────────────────────────────────────── */
int pat_load(void) {
    if (!g_initialized) {
        pat_init();
    }
    
    const char *path = "persist/patterns.bin";
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("  PAT: no saved patterns at %s (fresh start)\n", path);
        return -1;
    }
    
    uint32_t count;
    fread(&count, sizeof(count), 1, f);
    fread(&g_learner.learn_cycles, sizeof(g_learner.learn_cycles), 1, f);
    
    if (count > PATTERN_MAX_SLOTS) count = PATTERN_MAX_SLOTS;
    
    memset(g_learner.slots, 0, sizeof(g_learner.slots));
    for (uint32_t i = 0; i < count; i++) {
        pattern_t p;
        fread(&p, sizeof(pattern_t), 1, f);
        if (i < PATTERN_MAX_SLOTS) {
            g_learner.slots[i] = p;
            g_learner.total_patterns++;
        }
    }
    
    fclose(f);
    printf("  PAT: loaded %u patterns from %s (cycle=%u)\n",
           count, path, g_learner.learn_cycles);
    return 0;
}

/* ── 清理 ─────────────────────────────────────────────────── */
void pat_cleanup(void) {
    memset(&g_learner, 0, sizeof(g_learner));
    g_initialized = 0;
    printf("  PAT: pattern learner cleaned up\n");
}

/* ── Record a remote pattern (from distributed blackboard) ── */
void pat_record_remote(uint8_t stress_low, uint8_t stress_high,
                        int8_t drive_min, int8_t drive_max,
                        uint8_t action_type, int8_t avg_outcome,
                        uint16_t sample_count) {
    if (!g_initialized) return;
    
    /* Create a synthetic experience that represents the remote pattern */
    /* We'll add it as if it were observed locally */
    for (int8_t drive = drive_min; drive <= drive_max; drive++) {
        for (uint8_t stress = stress_low; stress <= stress_high; stress++) {
            pattern_key_t key;
            key.hw_stress    = stress;
            key.drive_bucket = quantize_drive(drive);
            key.gen_bucket   = 0;  /* unknown gen from remote */
            key.action_type  = action_type;
            
            pattern_t *p = find_or_create_slot(key);
            if (!p) continue;
            
            p->total_outcome += avg_outcome * sample_count;
            p->sample_count  += sample_count;
            if (avg_outcome < 0)
                p->total_crashes += sample_count / 2;
            p->avg_outcome   = (float)p->total_outcome / p->sample_count;
            p->crash_rate    = (float)p->total_crashes / p->sample_count;
            p->last_seen_tick = 0;
        }
    }
    g_learner.learn_cycles++;
}
