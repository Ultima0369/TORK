#include "sched_fission_branch.h"
#include "soul_access.h"
#include "fission.h"
#include "dispatch.h"
#include "blackboard.h"
#include "../learning/branch.h"
#include "../learning/pi_seed.h"
#include "../learning/pi_index.h"
#include <string.h>

/* ── Fission (every 1000) ── */
void tick_fission(sched_ctx_t *ctx) {
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    if (i <= 0) return;
    if (!fission_decide(soul) || ctx->inst.curiosity <= 0.6f) {
        printf("[%4d] tick=%-6u FISSION: conditions not met (tick=%u drive=%+d curiosity=%.1f)\n",
               i, inp->tick, inp->tick, drive, ctx->inst.curiosity);
        return;
    }

    printf("[%4d] tick=%-6u FISSION: instinct triggers fission\n", i, inp->tick);
    dispatch_input_t fdin;
    memset(&fdin, 0, sizeof(fdin));
    fdin.action = DISP_SELF_FISSION;
    fdin.input = "fission_trigger";
    fdin.tick = inp->tick;
    fdin.hw_stress = inp->hw_stress;
    fdin.drive = (int8_t)drive;
    fdin.gen_count = inp->fission_count;
    dispatch_output_t fdout = tork_dispatch(&fdin);
    (void)fdout;

    pid_t child = fission_spawn();
    if (child > 0) {
        fdout.rc = 0;
        uint16_t cp = (uint16_t)child;
        soul_write_buf(soul, S_CHILD_PID, &cp, 2);
        uint8_t fc = inp->fission_count + 1;
        soul_write_buf(soul, S_FISSION_COUNT, &fc, 1);
        uint16_t ft = (uint16_t)inp->tick;
        soul_write_buf(soul, S_FISSION_TICK, &ft, 2);
        inp->fission_count = fc;
        printf("[%4d] tick=%-6u FISSION: child spawned PID=%d\n", i, inp->tick, child);
        bb_write(BB_TYPE_FISSION, 1, (uint32_t)child);
        bb_inc_fissions();

        if (fission_migrate(child) == 0) {
            uint16_t w = inp->wins + 1;
            soul_write_buf(soul, S_WINS, &w, 2);
            inp->wins = w;
            uint16_t zero16 = 0;
            soul_write_buf(soul, S_CHILD_PID, &zero16, 2);
            printf("[%4d] tick=%-6u FISSION: parent won, wins=%d\n", i, inp->tick, w);
        }
    } else {
        printf("[%4d] tick=%-6u FISSION: spawn failed\n", i, inp->tick);
    }
}

/* ── Branch fork + advance ── */
void tick_branch(sched_ctx_t *ctx) {
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    int drive = ctx->drive;

    {
        fork_request_t fork_req;
        fork_req.should_fork = 0;
        fork_req.current_tick = inp->tick;
        fork_req.gen_count = soul_gen_count(soul);
        fork_req.drive = drive;
        fork_req.sandbox_level = inp->mode;
        fork_req.branch_cool_tick = br_last_fork_tick();

        if (!ctx->rhythm_inited) pi_rhythm_init(&ctx->rhythm);
        float dissonance = pi_rhythm_dissonance(&ctx->rhythm);
        if (br_should_fork(&fork_req, dissonance)) {
            int slot = br_fork(soul, inp->tick, soul_gen_count(soul));
            if (slot >= 0)
                printf("[%4d] tick=%-6u BRANCH: fork created at slot %d (drive=%d)\n",
                       i, inp->tick, slot, drive);
        }
    }
    br_advance_all();
}
