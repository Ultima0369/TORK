#include "replay.h"
#include "experience.h"
#include "pattern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── 全局 ──────────────────────────────────────────────────── */
static replay_result_t g_last_replay;

/* ── 工具：模拟替代行动的结果 ──────────────────────────────────
 *  给定原始经验和替代行动类型，估算如果当时走了那条路会怎样。
 *  这不是精确计算，而是基于 pattern 引擎的预测 + 随机扰动。
 *  就像人类在梦里「感觉」事情可能会怎样发展。 
 * ──────────────────────────────────────────────────────────── */
static int simulate_alternative(const experience_t *original, 
                                 uint8_t alt_action, 
                                 float *improvement) {
    /* 用 pattern 引擎预测替代行动的 outcome */
    float predicted = pat_predict_outcome(
        original->hw_stress, 
        original->drive_pre, 
        original->gen_count, 
        alt_action);
    
    /* 如果没有历史数据，用经验法则估算 */
    if (predicted == 0.0f) {
        /* 保守估算：替代行动通常比已知的好或差不明确 */
        predicted = (float)(rand() % 40 - 20);  /* -20..+19 */
    }
    
    float actual = original->outcome;
    *improvement = predicted - actual;
    
    return (predicted > actual + 5) ? 1 : 0;  /* 明显更好 = 发现 */
}

/* ── 执行一次深度回放 ──────────────────────────────────────── */
replay_result_t replay_deep(void) {
    replay_result_t result;
    memset(&result, 0, sizeof(result));
    
    int total = exp_count();
    if (total < 5) {
        printf("  RPL: too few experiences (%d) for deep replay\\n", total);
        return result;
    }
    
    /* 先获取一批经验 */
    experience_t batch[REPLAY_BATCH_SIZE];
    int n = (total < REPLAY_BATCH_SIZE) ? total : REPLAY_BATCH_SIZE;
    
    /* 随机采样：从不同位置读取，覆盖不同时期 */
    int stride = total / n;
    if (stride < 1) stride = 1;
    
    int loaded = 0;
    for (int i = 0; i < n; i++) {
        int idx = (i * stride + rand() % stride) % total;
        if (exp_read(idx, &batch[loaded]) == 0) {
            loaded++;
        }
    }
    
    if (loaded < 3) {
        printf("  RPL: only loaded %d experiences, abort\\n", loaded);
        return result;
    }
    
    result.experiences_played = loaded;
    
    /* 对每条经验，尝试替代行动 */
    for (int i = 0; i < loaded; i++) {
        experience_t *exp = &batch[i];
        
        /* 原行动的 outcome 已知 */
        /* 尝试所有 7 种行动类型（除了原行动本身） */
        for (uint8_t alt = 0; alt < 7; alt++) {
            if (alt == exp->action_type) continue;
            
            float improvement = 0.0f;
            int found = simulate_alternative(exp, alt, &improvement);
            
            if (found) {
                result.alternative_count++;
                if (improvement > result.best_improvement) {
                    result.best_improvement = improvement;
                    result.best_action = (int8_t)alt;
                    result.best_context_stress = exp->hw_stress;
                    result.best_context_drive = exp->drive_pre;
                }
            }
        }
    }
    
    /* 检查新发现是否已经在 pattern 库中 */
    if (result.best_action >= 0 && result.best_improvement > 10) {
        float existing = pat_predict_outcome(
            result.best_context_stress,
            result.best_context_drive,
            0, result.best_action);
        
        if (existing < result.best_improvement * 0.5f) {
            result.new_insights = 1;
        }
    }
    
    result.active = 1;
    
    printf("  RPL: deep replayed %d experiences, found %d alternatives",
           result.experiences_played, result.alternative_count);
    if (result.new_insights) {
        printf(", **NEW INSIGHT**: action %d could be +%.0f better at (stress=%d, drive=%d)",
               result.best_action, result.best_improvement,
               result.best_context_stress, result.best_context_drive);
    }
    printf("\\n");
    
    g_last_replay = result;
    return result;
}

/* ── 获取上次回放结果 ──────────────────────────────────────── */
replay_result_t replay_last_result(void) {
    return g_last_replay;
}
