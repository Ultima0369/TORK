#include "branch.h"
#include "experience.h"
#include "pi_seed.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── 全局状态 ─────────────────────────────────────────────── */
static branch_context_t g_branches[BRANCH_MAX_ACTIVE];
static int g_branch_count = 0;
static uint32_t g_last_fork_tick = 0;
static uint32_t g_next_branch_id = 1;  /* 0 保留给主干 */
static reap_report_t g_last_reap;
static int g_initialized = 0;

/* ── 内部: 找空闲槽位 ─────────────────────────────────────── */
static int find_free_slot(void) {
    for (int i = 0; i < BRANCH_MAX_ACTIVE; i++) {
        if (!g_branches[i].active) return i;
    }
    return -1; /* 全满 */
}

/* ── 内部: 生成 Soul 快照 ─────────────────────────────────── */
static void snapshot_soul(soul_t *dest, soul_t *src) {
    memcpy(dest->buf, src->buf, SOUL_SIZE);
    dest->pid = src->pid;
    dest->mem_fd = -1;  /* 不共享 fd */
    dest->wr_fd = -1;
}

/* ── 初始化 ───────────────────────────────────────────────── */
void br_init(void) {
    memset(g_branches, 0, sizeof(g_branches));
    memset(&g_last_reap, 0, sizeof(g_last_reap));
    g_branch_count = 0;
    g_last_fork_tick = 0;
    g_next_branch_id = 1;
    g_initialized = 1;
    printf("  BR: branch engine initialized (%d slots)\n", BRANCH_MAX_ACTIVE);
}

/* ── 检查是否可以分岔 ─────────────────────────────────────── */
int br_should_fork(const fork_request_t *req, float rhythm_dissonance) {
    if (!req) return 0;

    /* 1. 驱动值必须为正（好奇心驱动，不是恐惧驱动） */
    if (req->drive <= 0) return 0;

    /* 2. 沙箱级别至少 2（有权写自己的代码/内存） */
    if (req->sandbox_level < 2) return 0;

    /* 3. 冷却期：距离上次分岔至少 BRANCH_COOLDOWN_TICKS */
    { int32_t tick_diff = (int32_t)(req->current_tick - req->branch_cool_tick); if (tick_diff < 0 || (uint32_t)tick_diff < BRANCH_COOLDOWN_TICKS) return 0; }

    /* 4. 经验足够丰富（至少 100 条经验才值得分岔试探）
     *    节律失调 > 0.6 时降低阈值 30%，增强分岔急切度 */
    uint32_t exp_threshold = 100;
    if (rhythm_dissonance > 0.6f) {
        exp_threshold = (uint32_t)(exp_threshold * 0.7f);
    }
    if (exp_count() < exp_threshold) return 0;
    
    /* 5. 有空闲槽位 */
    if (g_branch_count >= BRANCH_MAX_ACTIVE) return 0;
    
    return 1;
}

/* ── 创建分支 ─────────────────────────────────────────────── */
int br_fork(soul_t *parent_soul, uint32_t current_tick, uint32_t gen_count) {
    if (!parent_soul || !g_initialized) return -1;
    
    int slot = find_free_slot();
    if (slot < 0) return -1;
    
    branch_context_t *branch = &g_branches[slot];
    memset(branch, 0, sizeof(branch_context_t));
    
    /* 1. 继承主干灵魂（全量复制） */
    snapshot_soul(&branch->soul, parent_soul);
    
    /* 2. 修改分岔身份字段 */
    uint32_t branch_id = g_next_branch_id++;
    
    /* 获取主干 branch_id 作为 parent_id */
    uint32_t main_branch_id = 0;
    memcpy(&main_branch_id, parent_soul->buf + S_BRANCH_ID, 4);
    
    /* 写分支 Soul 的字段 */
    memcpy(branch->soul.buf + S_BRANCH_ID, &branch_id, 4);
    memcpy(branch->soul.buf + S_PARENT_ID, &main_branch_id, 4);
    memcpy(branch->soul.buf + S_BRANCH_GEN, &gen_count, 4);
    
    uint32_t max_ticks = BRANCH_DEFAULT_LIFETIME;
    memcpy(branch->soul.buf + S_MAX_TICKS, &max_ticks, 4);
    
    uint64_t death_none = DEATH_NONE;
    memcpy(branch->soul.buf + S_DEATH_REPORT, &death_none, 8);
    
    uint64_t soul_addr = (uint64_t)(uintptr_t)&branch->soul;
    memcpy(branch->soul.buf + S_BRANCH_SOUL_PTR, &soul_addr, 8);
    
    uint32_t zero = 0;
    memcpy(branch->soul.buf + S_BRANCH_TICKS, &zero, 4);
    
    int16_t drive_val = 0;
    memcpy(branch->soul.buf + S_BRANCH_DRIVE_PEAK, &drive_val, 2);
    memcpy(branch->soul.buf + S_BRANCH_DRIVE_END, &drive_val, 2);
    
    /* 3. 填充上下文 */
    branch->active = 1;
    branch->ticks_lived = 0;
    branch->branch_id = branch_id;
    branch->parent_id = main_branch_id;
    
    g_last_fork_tick = current_tick;
    g_branch_count++;
    
    printf("  BR: fork #%u (parent=%u, slot=%d, lifetime=%u ticks)\n",
           branch_id, main_branch_id, slot, max_ticks);
    
    return slot;
}

/* ── 执行分支的一个 tick（轻量模拟） ────────────────────────── */
static void execute_branch_tick(branch_context_t *branch) {
    /* 读取分支 Soul 中的关键值 */
    uint8_t  hw_stress  = branch->soul.buf[S_HW_STRESS];
    (void)hw_stress;
    int8_t   drive      = (int8_t)branch->soul.buf[S_DRIVE];
    uint32_t tick;
    memcpy(&tick, branch->soul.buf + S_TICK, 4);
    
    /* 模拟：驱动值小幅随机波动（分支的「试探」本质） */
    int8_t drive_delta = (int8_t)(pi_seed_from_tsc() % 7 - 3);  /* -3..+3 */
    int new_drive = drive + drive_delta;
    if (new_drive > 127) new_drive = 127;
    if (new_drive < -128) new_drive = -128;
    branch->soul.buf[S_DRIVE] = (uint8_t)(int8_t)new_drive;
    
    /* 更新 tick */
    tick++;
    memcpy(branch->soul.buf + S_TICK, &tick, 4);
    
    /* 更新分支已存活 tick */
    uint32_t bt;
    memcpy(&bt, branch->soul.buf + S_BRANCH_TICKS, 4);
    bt++;
    memcpy(branch->soul.buf + S_BRANCH_TICKS, &bt, 4);
    
    /* 记录 drive 峰值 */
    int16_t peak;
    memcpy(&peak, branch->soul.buf + S_BRANCH_DRIVE_PEAK, 2);
    if (new_drive > peak) {
        peak = (int16_t)new_drive;
        memcpy(branch->soul.buf + S_BRANCH_DRIVE_PEAK, &peak, 2);
    }
    
    branch->ticks_lived = bt;
    
    /* 更新分支体验计数 */
    uint32_t ec;
    memcpy(&ec, branch->soul.buf + S_EXPERIENCE_COUNT, 4);
    ec++;
    memcpy(branch->soul.buf + S_EXPERIENCE_COUNT, &ec, 4);
}

/* ── 推进所有活跃分支 ─────────────────────────────────────── */
void br_advance_all(void) {
    if (!g_initialized) return;
    
    for (int i = 0; i < BRANCH_MAX_ACTIVE; i++) {
        if (!g_branches[i].active) continue;
        
        branch_context_t *branch = &g_branches[i];
        
        /* 执行一个 tick */
        execute_branch_tick(branch);
        
        /* 检查 CRC 校验 */
        /* 简化版：不实际执行完整 CRC，仅检查 Soul 基本完整性 */
        uint32_t max_ticks;
        memcpy(&max_ticks, branch->soul.buf + S_MAX_TICKS, 4);
        
        /* 检查是否超时 */
        uint32_t bt;
        memcpy(&bt, branch->soul.buf + S_BRANCH_TICKS, 4);
        
        if (bt >= max_ticks) {
            /* 大限已到 */
            reap_report_t r = br_reap(i, DEATH_TIMEOUT);
            printf("  BR: branch #%u reaped (TIMEOUT, lived=%lu ticks, final_drive=%d)\n",
                   r.branch_id, r.ticks_lived, r.final_drive);
        }
        
        /* 检查驱动值崩溃 */
        int8_t drive = (int8_t)branch->soul.buf[S_DRIVE];
        if (drive < -100) {
            reap_report_t r = br_reap(i, DEATH_DRIVE_COLLAPSE);
            printf("  BR: branch #%u reaped (DRIVE_COLLAPSE, drive=%d)\n",
                   r.branch_id, drive);
        }
    }
}

/* ── 收割分支 ─────────────────────────────────────────────── */
reap_report_t br_reap(int branch_index, uint64_t death_reason) {
    reap_report_t report;
    memset(&report, 0, sizeof(report));
    
    if (branch_index < 0 || branch_index >= BRANCH_MAX_ACTIVE) return report;
    if (!g_branches[branch_index].active) return report;
    
    branch_context_t *branch = &g_branches[branch_index];
    
    /* 1. 填充凋零报告 */
    report.branch_id    = branch->branch_id;
    report.parent_id    = branch->parent_id;
    report.death_reason = death_reason;
    report.ticks_lived  = branch->ticks_lived;
    
    memcpy(&report.final_drive, branch->soul.buf + S_DRIVE, 1);
    memcpy(&report.drive_peak, branch->soul.buf + S_BRANCH_DRIVE_PEAK, 2);
    memcpy(&report.drive_end, branch->soul.buf + S_BRANCH_DRIVE_END, 2);
    
    /* 2. 判断是否值得将来重新试探 */
    int8_t initial_drive;
    memcpy(&initial_drive, branch->soul.buf + S_DRIVE, 1); /* 用当前 drive 近似 */
    
    /* 如果分支存活期间 drive 峰值显著高于初始值 */
    if (report.drive_peak > initial_drive * 2 && report.ticks_lived > 100) {
        report.promising = 1;
    }
    
    /* 3. 写死因到分支 Soul */
    memcpy(branch->soul.buf + S_DEATH_REPORT, &death_reason, 8);
    memcpy(branch->soul.buf + S_BRANCH_DRIVE_END, &report.drive_peak, 2);
    
    /* 4. 将死亡记录为经验 */
    uint32_t tick;
    memcpy(&tick, branch->soul.buf + S_TICK, 4);
    
    /* 主干经验记录用当前 tick 和分支的最终状态 */
    uint8_t hw_stress = branch->soul.buf[S_HW_STRESS];
    int8_t drive_end = report.drive_peak;
    
    uint16_t gen;
    memcpy(&gen, branch->soul.buf + S_BRANCH_GEN, 2);
    
    /* action_type=7 保留给分支死亡事件 */
    exp_record(tick, hw_stress, initial_drive, gen,
               7,  /* action_type: BRANCH_DEATH */
               (int8_t)death_reason,
               report.drive_peak > 20 ? 30 : -10,  /* outcome: 正向则有益 */
               (death_reason == DEATH_TIMEOUT) ? 0 : 1,  /* crash */
               0,  /* compile_ok */
               hw_stress, drive_end);
    
    /* 5. 释放资源 */
    branch->active = 0;
    g_branch_count--;
    
    g_last_reap = report;
    
    return report;
}

/* ── 合并回主干 ───────────────────────────────────────────── */
void br_merge_if_worthy(int branch_index, soul_t *parent_soul) {
    if (branch_index < 0 || branch_index >= BRANCH_MAX_ACTIVE) return;
    if (!g_branches[branch_index].active) return;
    if (!parent_soul) return;
    
    branch_context_t *branch = &g_branches[branch_index];
    
    /* 仅当分支自然超时死亡且表现出正向驱动提升时才合并 */
    uint64_t death;
    memcpy(&death, branch->soul.buf + S_DEATH_REPORT, 8);
    if (death != DEATH_TIMEOUT) return;
    
    /* 检查分支 drive 峰值是否优于主干当前值 */
    int16_t branch_peak;
    memcpy(&branch_peak, branch->soul.buf + S_BRANCH_DRIVE_PEAK, 2);
    
    int8_t main_drive = (int8_t)parent_soul->buf[S_DRIVE];
    
    int16_t threshold = (int16_t)(main_drive >= 0 ? main_drive * 12 / 10 : main_drive * 8 / 10);
    if (branch_peak <= threshold) return;  /* 不够好，不合并 */
    
    /* 非破坏性合并：以 learning_rate 为步长调整主干参数 */
    uint16_t lr;
    memcpy(&lr, parent_soul->buf + S_LEARNING_RATE, 2);
    if (lr == 0) lr = 500;  /* 默认学习率 */
    
    float step = lr / 1000.0f * 0.1f;  /* 极小的步长 */
    
    /* 合并 curiosity_decay */
    uint16_t branch_decay, main_decay;
    memcpy(&branch_decay, branch->soul.buf + S_CURIOSITY_DECAY, 2);
    memcpy(&main_decay, parent_soul->buf + S_CURIOSITY_DECAY, 2);
    
    if (branch_decay < main_decay) {
        /* 分支的衰减更慢（更好），向分支靠近 */
        uint16_t new_decay = main_decay - (uint16_t)((main_decay - branch_decay) * step);
        memcpy(parent_soul->buf + S_CURIOSITY_DECAY, &new_decay, 2);
        printf("  BR: merged curiosity_decay %u -> %u (from branch #%u)\n",
               main_decay, new_decay, branch->branch_id);
    }
    
    /* 合并 learning_rate */
    uint16_t blr;
    memcpy(&blr, branch->soul.buf + S_LEARNING_RATE, 2);
    if (blr > lr && blr <= 1000) {
        uint16_t new_lr = lr + (uint16_t)((blr - lr) * step);
        memcpy(parent_soul->buf + S_LEARNING_RATE, &new_lr, 2);
        printf("  BR: merged learning_rate %u -> %u (from branch #%u)\n",
               lr, new_lr, branch->branch_id);
    }
}

/* ── 获取活跃分支数 ───────────────────────────────────────── */
int br_active_count(void) {
    return g_branch_count;
}

/* ── 获取上次分岔 tick ────────────────────────────────────── */
uint32_t br_last_fork_tick(void) {
    return g_last_fork_tick;
}

/* ── 获取最近一次凋零报告 ─────────────────────────────────── */
reap_report_t br_last_reap(void) {
    return g_last_reap;
}

/* ── 清理 ─────────────────────────────────────────────────── */
void br_cleanup(void) {
    for (int i = 0; i < BRANCH_MAX_ACTIVE; i++) {
        if (g_branches[i].active) {
            br_reap(i, DEATH_EXEC_FAILURE);
        }
    }
    g_branch_count = 0;
    printf("  BR: all branches cleaned up\n");
}
