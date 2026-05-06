#include "sched_persist.h"
#include "soul_access.h"
#include "persistor.h"
#include "tln.h"
#include "../persist/code_archive.h"
#include "../code/strict_verifier.h"
#include <stdio.h>

/* ── Persistence (every 1000/5000/10000) ── */
void tick_persist(sched_ctx_t *ctx) {
    soul_t *soul = ctx->soul;
    instinct_input_t *inp = &ctx->inp;
    int i = ctx->round;
    if (i <= 0) return;

    if (i % 1000 == 0) {
        if (ps_save_all(soul->buf, SOUL_SIZE) == 0) {
            inp->save_success = 1;
            printf("[%4d] tick=%-6u PERSIST: state saved to disk\n", i, inp->tick);
        }
    }
    if (i % 5000 == 0) {
        if (ps_save_all(soul->buf, SOUL_SIZE) == 0) {
            inp->save_success = 1;
            printf("[%4d] tick=%-6u PERSIST: full save (params+rules)\n", i, inp->tick);
        }
        ps_decay_memory();
    }
    if (i % 10000 == 0) {
        ps_decay_memory();
    }

    /* TLN 持久化 (every 5000) */
    if (ctx->tln_enabled && i % 5000 == 0) {
        if (tln_save(&ctx->tln, NULL) == 0)
            printf("[%4d] tick=%-6u PERSIST: TLN saved\n", i, inp->tick);
    }

    /* CODE-4: 代码档案持久化 (every 1000) */
    if (i % 1000 == 0) {
        ca_save();
        sv_save();
    }
}
