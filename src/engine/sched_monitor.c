#include "sched_monitor.h"
#include "soul_access.h"
#include "monitor.h"
#include "../learning/watcher.h"
#include "../learning/self_build.h"
#include "../learning/mutation_guide.h"
#include "../learning/observer.h"
#include "../learning/energy.h"
#include "../learning/self_cal.h"
#include "../learning/snapshot.h"
#include "../learning/pi_seed.h"
#include "../learning/pi_index.h"
#include "../learning/branch.h"
#include "../learning/experience.h"
#include <stdio.h>
#include <string.h>

/* ── Watcher, self-build, mutation guide ── */
void tick_monitoring(sched_ctx_t *ctx) {
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    if (i > 0 && i % 10 == 0)
        watcher_scan_proc();

    if (i > 0 && i % 100 == 0) {
        int new_pats = watcher_learn_patterns();
        if (new_pats > 0)
            printf("[%4d] tick=%-6u WATCH: learned %d new patterns\n", i, inp->tick, new_pats);
    }

    if (i > 0 && i % 50 == 0) {
        int changed = sb_check_sources();
        if (changed > 0)
            printf("[%4d] tick=%-6u SELF: %d source files changed\n", i, inp->tick, changed);
    }

    if (i > 0 && i % 200 == 0 && drive > 0) {
        char rec[128];
        mg_strategy_type_t s = mg_recommend(rec, sizeof(rec));
        (void)s;
        if (i % 1000 == 0)
            printf("[%4d] tick=%-6u MGUIDE: recommend %s\n", i, inp->tick, rec);
    }
}

/* ── Observer + Energy ── */
void tick_observer_energy(sched_ctx_t *ctx) {
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    if (inp->tick % OBS_SAMPLE_INTERVAL == 0) {
        obs_sample(inp->tick, inp->hw_stress, (int8_t)drive, 47, 30,
                   (uint16_t)br_active_count());
        int anomaly = obs_check_anomaly(inp->hw_stress, (int8_t)drive, 47, 30);
        if (anomaly > 1)
            printf("[%4d] tick=%-6u OBS: anomaly detected (flags=%d)\n", i, inp->tick, anomaly);
    }

    {
        float hb_mult = self_cal_tick(inp->tick, 30.0f, 4096.0f, inp->hw_stress, exp_count(), 0);
        eng_update(30.0f, 4096.0f, 0.1f, (drive == 0));
        if (hb_mult != 1.0f && (i % 10 == 0))
            printf("[%4d] tick=%-6u ENG: heartbeat mult=%.1f\n", i, inp->tick, hb_mult);
        if (eng_should_limit_branches() && br_active_count() > 2) {
            /* don't fork when loaded */
        }
    }
}

/* ── Snapshot + health check ── */
void tick_snapshot(sched_ctx_t *ctx) {
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    {
        uint8_t soul_raw[SOUL_SIZE];
        memset(soul_raw, 0, SOUL_SIZE);
        memcpy(soul_raw, soul->buf, SOUL_SIZE);
        snap_auto(inp->tick, (int64_t)drive, inp->hw_stress,
                  soul_gen_count(soul), soul_raw);
    }

    {
        health_check_t hc = snap_health_check(inp->tick, (int64_t)drive, inp->hw_stress, 1);
        if (!hc.degraded && inp->tick > 0 && inp->tick % (SNAP_AUTO_INTERVAL * 8) == 0) {
            uint8_t soul_raw[SOUL_SIZE];
            memset(soul_raw, 0, SOUL_SIZE);
            memcpy(soul_raw, soul->buf, SOUL_SIZE);
            snap_commit(inp->tick, (int64_t)drive, inp->hw_stress,
                       soul_gen_count(soul), soul_raw);
        }
        if (hc.degraded && soul_self_pid(soul) > 0) {
            printf("[%4d] tick=%-6u SNAP: DEGRADED (drive_drop=%.0f), ROLLING BACK\n",
                   i, inp->tick, hc.drive_drop);
            uint8_t restored_soul[SOUL_SIZE];
            uint32_t restore_tick = 0;
            int r = snap_rollback(restored_soul, &restore_tick);
            if (r > 0 && restore_tick > 0 && soul->wr_fd >= 0) {
                soul_write_buf(soul, 0, restored_soul, SOUL_SIZE);
                printf("[%4d] tick=%-6u SNAP: wrote %d bytes to core via ptrace\n",
                       i, inp->tick, r);
                soul_read(soul);
            }
        }
    }
}

/* ── π-seed + rhythm ── */
void tick_pi_rhythm(sched_ctx_t *ctx) {
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int *drive = &ctx->drive;

    if (!ctx->rhythm_inited) {
        pi_rhythm_init(&ctx->rhythm);
        ctx->rhythm_inited = 1;
    }

    uint8_t pi_val = pi_seed_hmac();
    float pi_mid = pi_seed_float();
    static uint8_t prev_pi_val = 0;

    (void)pi_drift(pi_val, prev_pi_val);

    if (*drive == 0 || *drive == DRIVE_MAX || *drive == DRIVE_MIN) {
        int8_t nudge = (int8_t)(pi_mid * 5.0f) - 2;
        int new_drive = *drive + nudge;
        if (new_drive > DRIVE_MAX) new_drive = DRIVE_MAX;
        if (new_drive < DRIVE_MIN) new_drive = DRIVE_MIN;
        soul_write_byte(soul, S_DRIVE, (uint8_t)(int8_t)new_drive);
        *drive = (int8_t)new_drive;
    }

    {
        int8_t drive_delta = (int8_t)*drive - ctx->prev_drive_val;
        pi_rhythm_observe(&ctx->rhythm, pi_val, drive_delta);

        float dissonance = pi_rhythm_dissonance(&ctx->rhythm);
        if (dissonance > 0.7f && i % 50 == 0) {
            int8_t kick = (int8_t)(pi_mid * 20.0f) - 10;
            int new_drive = *drive + kick;
            if (new_drive > DRIVE_MAX) new_drive = DRIVE_MAX;
            if (new_drive < DRIVE_MIN) new_drive = DRIVE_MIN;
            soul_write_byte(soul, S_DRIVE, (uint8_t)(int8_t)new_drive);
            *drive = (int8_t)new_drive;
        }

        if (ctx->rhythm.count >= 8 && i % 20 == 0) {
            r_zone_t zone = pi_r_zone(&ctx->rhythm);
            if (zone == R_DEAD && !ctx->quiet) {
                printf("  R-ZONE: DEAD (R<3.0) — 打破僵死\n");
                int8_t kick = (int8_t)(pi_mid * 40.0f) - 20;
                int new_drive = *drive + kick;
                if (new_drive > DRIVE_MAX) new_drive = DRIVE_MAX;
                if (new_drive < DRIVE_MIN) new_drive = DRIVE_MIN;
                soul_write_byte(soul, S_DRIVE, (uint8_t)(int8_t)new_drive);
                *drive = (int8_t)new_drive;
            } else if (zone == R_WINDOW && !ctx->quiet && i % 100 == 0) {
                printf("  R-ZONE: WINDOW — 周期窗口，积极学习\n");
            }
        }

        if (ctx->rhythm.count >= 8 && i % 10 == 0) {
            pi_profile_t prof = pi_profile_from_rhythm(&ctx->rhythm);
            pidx_add(&prof, inp->tick, (uint8_t)inp->hw_stress);
            pi_match_t m = pidx_query(&prof, 0.6f);
            if (m.slot_idx >= 0 && !ctx->quiet)
                printf("  PIDX: rhythm match sim=%.3f cat=%u ref=%u\n",
                       m.similarity, m.category, m.ref_id);
        }
    }

    prev_pi_val = pi_val;
}
