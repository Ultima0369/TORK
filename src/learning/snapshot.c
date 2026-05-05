#include "snapshot.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

/* ── 全局 ──────────────────────────────────────────────────── */
static snapshot_history_t g_hist;
static int g_initialized = 0;
static uint64_t g_last_auto_tick = 0;

/* ── 简易 CRC32 ────────────────────────────────────────────── */
static uint32_t crc32_simple(const uint8_t *data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else         crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

/* ── 初始化 ───────────────────────────────────────────────── */
void snap_init(void) {
    memset(&g_hist, 0, sizeof(g_hist));
    g_initialized = 1;
    printf("  SNAP: snapshot engine initialized (%d slots)\n", SNAP_MAX_HISTORY);
}

/* ── 内部：保存快照 ─────────────────────────────────────────── */
static void save_snapshot(uint64_t tick, int64_t drive, uint8_t hw_stress,
                           uint64_t gen_count, const uint8_t *soul_data) {
    snapshot_t *s = &g_hist.slots[g_hist.head];
    s->tick      = tick;
    s->drive     = drive;
    s->hw_stress = hw_stress;
    s->gen_count = gen_count;
    memcpy(s->soul_data, soul_data, SOUL_SIZE);
    s->checksum  = crc32_simple((const uint8_t *)s, sizeof(snapshot_t) - 4);
    
    g_hist.head = (g_hist.head + 1) % SNAP_MAX_HISTORY;
    if (g_hist.count < SNAP_MAX_HISTORY) g_hist.count++;
    g_last_auto_tick = tick;
}

/* ── 自动快照 ──────────────────────────────────────────────── */
void snap_auto(uint64_t tick, int64_t drive, uint8_t hw_stress,
               uint64_t gen_count, const uint8_t *soul_data) {
    if (!g_initialized) return;
    if (tick - g_last_auto_tick < SNAP_AUTO_INTERVAL) return;
    
    save_snapshot(tick, drive, hw_stress, gen_count, soul_data);
}

/* ── 强制快照 ──────────────────────────────────────────────── */
void snap_force(uint64_t tick, int64_t drive, uint8_t hw_stress,
                uint64_t gen_count, const uint8_t *soul_data) {
    if (!g_initialized) return;
    if (tick - g_last_auto_tick < SNAP_MIN_TICKS_BETWEEN) return;
    
    save_snapshot(tick, drive, hw_stress, gen_count, soul_data);
    printf("  SNAP: forced snapshot at tick %lu\\n", tick);
}

/* ── 健康检查 ──────────────────────────────────────────────── */

/* 修复：只在 drive 真正恶化时触发回滚，而不是从初始峰值正常回落 */
health_check_t snap_health_check(uint64_t tick, int64_t drive,
                                  uint8_t hw_stress, int soul_crc_ok) {
    (void)tick;
    health_check_t result;
    memset(&result, 0, sizeof(result));
    
    if (!g_initialized || g_hist.count < 2) return result;
    
    /* 获取最新快照 */
    int last_idx = (g_hist.head - 1 + SNAP_MAX_HISTORY) % SNAP_MAX_HISTORY;
    snapshot_t *last = &g_hist.slots[last_idx];
    
    /* 检查 drive 是否大幅下降 — 但只在 drive 跌到负值时触发
     * 正常从峰值(如+127)回落到稳定值(如+75)不是退化
     * 从正值跌到负值或从正常值跌到远低于历史均值才是退化 */
    if (drive < last->drive - 30 && drive < -20) {
        result.drive_drop = (float)(last->drive - drive);
        result.degraded = 1;
    }
    /* 也检测：drive 持续为负且越来越差 */
    if (drive < 0 && last->drive < 0 && drive < last->drive - 10) {
        if (!result.degraded) result.drive_drop = (float)(last->drive - drive);
        result.degraded = 1;
    }
    
    /* 检查 stress 是否飙升（保留，这是真正的硬件问题） */
    if (hw_stress > last->hw_stress + 1 && hw_stress >= 3) {
        result.stress_spike = 1;
        result.degraded = 1;
    }
    
    /* 检查 Soul CRC 是否失败（保留，这是数据损坏） */
    if (!soul_crc_ok) {
        result.soul_crc_fail = 1;
        result.degraded = 1;
    }
    
    /* 找到最佳回滚目标：drive 最高的已提交快照优先，否则最高 drive */
    if (result.degraded) {
        int64_t best_drive = -9999;
        result.best_snapshot_idx = 0;
        
        for (uint32_t i = 0; i < g_hist.count; i++) {
            if (g_hist.slots[i].drive > best_drive) {
                best_drive = g_hist.slots[i].drive;
                result.best_snapshot_idx = i;
            }
        }
        
        printf("  SNAP: health DEGRADED (drive_drop=%.0f, stress=%d, crc=%d)\n",
               result.drive_drop, result.stress_spike, result.soul_crc_fail);
        printf("  SNAP: best snapshot at tick %lu (drive=%ld)\n",
               g_hist.slots[result.best_snapshot_idx].tick,
               g_hist.slots[result.best_snapshot_idx].drive);
    }
    
    return result;
}
/* ── 回滚 ──────────────────────────────────────────────────── */
int snap_rollback(uint8_t *soul_data, uint32_t *restore_tick) {
    if (!g_initialized || g_hist.count == 0) return 0;
    
    /* 找到 drive 最高的快照 */
    int64_t best_drive = -9999;
    int best_idx = 0;
    
    for (uint32_t i = 0; i < g_hist.count; i++) {
        if (g_hist.slots[i].drive > best_drive) {
            best_drive = g_hist.slots[i].drive;
            best_idx = i;
        }
    }
    
    snapshot_t *best = &g_hist.slots[best_idx];
    memcpy(soul_data, best->soul_data, SOUL_SIZE);
    if (restore_tick) *restore_tick = (uint32_t)best->tick;
    
    g_hist.restores++;
    g_hist.last_restore_tick = (uint32_t)best->tick;
    
    printf("  SNAP: ROLLBACK to tick %lu (drive=%ld) [restore #%u]\\n",
           best->tick, best->drive, g_hist.restores);
    
    return 1;
}

/* ── 获取上次回滚 tick ──────────────────────────────────────── */
uint32_t snap_last_restore(void) {
    return g_hist.last_restore_tick;
}

/* ── 获取回滚次数 ────────────────────────────────────────────── */
uint32_t snap_restore_count(void) {
    return g_hist.restores;
}

/* ── 保存 ──────────────────────────────────────────────────── */
int snap_save(void) {
    if (!g_initialized) return -1;
    
    FILE *f = fopen("persist/snapshots.bin", "wb");
    if (!f) return -1;
    
    fwrite(&g_hist, sizeof(g_hist), 1, f);
    fclose(f);
    return 0;
}

/* ── 加载 ──────────────────────────────────────────────────── */
int snap_load(void) {
    if (!g_initialized) snap_init();
    
    FILE *f = fopen("persist/snapshots.bin", "rb");
    if (!f) return -1;
    
    fread(&g_hist, sizeof(g_hist), 1, f);
    fclose(f);
    printf("  SNAP: loaded %u snapshots from disk\\n", g_hist.count);
    return 0;
}

/* ── 打印状态 ──────────────────────────────────────────────── */
void snap_print_status(void) {
    if (!g_initialized) return;
    
    printf("  SNAP: %u snapshots, %u restores\\n", g_hist.count, g_hist.restores);
    for (uint32_t i = 0; i < g_hist.count; i++) {
        printf("       [%u] tick=%lu drive=%ld stress=%u gen=%lu\\n",
               i, g_hist.slots[i].tick, g_hist.slots[i].drive,
               g_hist.slots[i].hw_stress, g_hist.slots[i].gen_count);
    }
}

/* ── 提交当前快照为已确认健康 ──────────────────────────────── */
void snap_commit(uint64_t tick, int64_t drive, uint8_t hw_stress,
                 uint64_t gen_count, const uint8_t *soul_data) {
    if (!g_initialized) return;
    save_snapshot(tick, drive, hw_stress, gen_count, soul_data);
    g_hist.commits++;
    g_hist.last_commit_tick = (uint32_t)tick;
    if (g_hist.commit_interval == 0) g_hist.commit_interval = 50;
    else if (g_hist.commit_interval < 800) g_hist.commit_interval *= 2;
    printf("  SNAP: COMMIT #%u at tick=%lu (drive=%ld, interval=%u)\n",
           g_hist.commits, tick, drive, g_hist.commit_interval);
}

uint32_t snap_committed_count(void) {
    return g_hist.commits;
}
