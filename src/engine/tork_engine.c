#include "soul_access.h"
#include "instinct.h"
#include "monitor.h"
#include "fission.h"
#include "blackboard.h"
#include "inductor.h"
#include "persistor.h"
#include "dispatch.h"
#include "codegen.h"
#include "scheduler.h"
#include "../learning/experience.h"
#include "../learning/branch.h"
#include "../learning/pattern.h"
#include "../learning/self_tune.h"
#include "../learning/observer.h"
#include "../learning/snapshot.h"
#include "../learning/energy.h"
#include "../learning/watcher.h"
#include "../learning/self_build.h"
#include "../learning/mutation_guide.h"
#include "../learning/distributed.h"
#include "../learning/pi_seed.h"
#include "../learning/pi_index.h"
#include "torkd.h"
#include "task.h"
#include "auditor.h"
#include "../learning/self_cal.h"
#include "../grid/grid_soul_connector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static pid_t core_pid = 0;
static int do_restore = 0;
static int restored_files = 0;

static void cleanup_core(int sig) {
    (void)sig;
    if (core_pid > 0) {
        kill(core_pid, SIGTERM);
        waitpid(core_pid, NULL, 0);
    }
    torkd_shutdown();
    dist_cleanup();
    grid_engine_cleanup();
    ps_emergency_save();
    snap_save();
    watcher_save();
    sb_save();
    mg_save();
    obs_save_baseline();
    pat_save();
    pat_cleanup();
    pidx_save();
    pidx_cleanup();
    task_cleanup();
    br_cleanup();
    exp_save();
    _exit(0);
}

static int start_core(void) {
    core_pid = fork();
    if (core_pid == 0) {
        FILE *dn = fopen("/dev/null", "w");
        if (dn) { dup2(fileno(dn), STDOUT_FILENO); fclose(dn); }
        execl("build/tork_core", "tork_core", NULL);
        _exit(1);
    }
    if (core_pid < 0) return -1;
    usleep(200000);
    return 0;
}

/* ── Subsystem initialization ── */
static void init_subsystems(soul_t *soul) {
    if (bb_init() != 0)
        fprintf(stderr, "warning: bb_init failed — blackboard unavailable\n");
    exp_init();
    br_init();
    pat_init();
    tune_init(1.0f, 0.7f, 1.15f);
    pat_load();
    pidx_init();
    pidx_load();
    obs_init();
    obs_load_baseline();
    snap_init();
    snap_load();
    eng_init();
    self_cal_init();
    watcher_init();
    watcher_load();
    sb_init();
    sb_load();
    mg_init();
    mg_load();
    pi_seed_init();
    dispatch_init();
    codegen_init();
    task_init();
    if (ind_init() != 0)
        fprintf(stderr, "warning: ind_init failed — inductor unavailable\n");
}

/* ── Restore from disk if requested ── */
static void do_restore_state(soul_t *soul) {
    if (!do_restore) return;
    restored_files = ps_restore_all();
    if (restored_files > 0)
        printf("TORK restored from disk (%d files recovered)\n", restored_files);
    else
        printf("TORK restore: no data found, fresh start\n");

    uint8_t soul_saved[SOUL_SIZE];
    size_t soul_got = ps_restore_soul(soul_saved, SOUL_SIZE);
    if (soul_got == SOUL_SIZE) {
        uint32_t saved_tick;
        memcpy(&saved_tick, soul_saved, 4);
        soul_write_buf(soul, S_TICK, &saved_tick, 4);
        restored_files++;
        printf("TORK restored soul: resuming from tick %u\n", saved_tick);
    }
}

/* ── Start network services ── */
static void init_services(soul_t *soul) {
    if (torkd_init(soul) == 0)
        printf("  TORKD: socket ready at %s\n", TORKD_SOCKET_PATH);
    else
        printf("  TORKD: socket init failed (non-fatal)\n");

    if (dist_init() != 0)
        printf("  DIST: network unavailable (non-fatal, running solo)\n");

    if (grid_engine_init() == 0)
        printf("  GRID: soul shared memory ready at /dev/shm/tork_soul.bin\n");
    else
        printf("  GRID: shared memory init failed (non-fatal)\n");
}

/* ── Write initial soul fields ── */
static void init_soul_fields(soul_t *soul) {
    {
        uint32_t pid_val = (uint32_t)core_pid;
        soul_write_buf(soul, S_SELF_PID, &pid_val, 4);
    }
    {
        uint32_t val;
        if (monitor_parse_proc_status(core_pid, "PPid:\t", &val) == 0) {
            uint16_t v = (val > 65535) ? 65535 : (uint16_t)val;
            soul_write_buf(soul, S_PPID, &v, 2);
        }
    }
    {
        uint8_t agreed = 0, sandbox_level = 0;
        FILE *agf = fopen("/etc/tork/agreement.sig", "rb");
        if (agf) {
            uint8_t agbuf[68];
            if (fread(agbuf, 1, 68, agf) == 68) {
                uint32_t magic;
                memcpy(&magic, agbuf, 4);
                if (magic == 0x4B524F54) {
                    agreed = agbuf[8];
                    sandbox_level = agbuf[12];
                }
            }
            fclose(agf);
        }
        soul_write_byte(soul, 0x48, agreed);
        soul_write_byte(soul, 0x49, sandbox_level);
        if (agreed == 1)
            printf("TORK agreement: ACCEPTED (sandbox level %d)\n", sandbox_level);
        else
            printf("TORK agreement: NONE — limited functionality\n");
    }
    {
        uint16_t lr = 500;
        soul_write_buf(soul, S_LEARNING_RATE, &lr, 2);
        uint16_t cd = 100;
        soul_write_buf(soul, S_CURIOSITY_DECAY, &cd, 2);
        uint32_t ec = exp_count();
        soul_write_buf(soul, S_EXPERIENCE_COUNT, &ec, 4);
        soul_write_buf(soul, S_EXPERIENCE_SAVED, &ec, 4);
    }
}

/* ── Main ── */
int main(int argc, char **argv) {
    int rounds = -1;
    int quiet = 0;
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--restore") == 0)
            do_restore = 1;
        else if (strcmp(argv[a], "--fresh") == 0)
            do_restore = 0;
        else if (strcmp(argv[a], "--daemon") == 0)
            rounds = -1;
        else if (strcmp(argv[a], "--quiet") == 0)
            quiet = 1;
        else {
            int v = atoi(argv[a]);
            if (v > 0) rounds = v;
        }
    }

    if (start_core() != 0) {
        fprintf(stderr, "start_core failed\n");
        return 1;
    }

    signal(SIGINT, cleanup_core);
    signal(SIGTERM, cleanup_core);

    soul_t soul;
    if (soul_open(&soul, core_pid) != 0) {
        fprintf(stderr, "soul_open failed — cannot read /proc/%d/mem\n", core_pid);
        kill(core_pid, SIGTERM);
        return 1;
    }

    init_subsystems(&soul);
    do_restore_state(&soul);
    init_services(&soul);
    init_soul_fields(&soul);

    printf("TORK engine started. core PID=%d\n", core_pid);
    printf("TORK v3.16 | π-heartbeat | generation at 0x54 | learn at 0x4C\n");
    printf("polling %dms | code 200 | modify 10 | optimize 30 | nop 50 | fission 1000 | persist 1000\n\n",
           tune_get_params().heartbeat_interval);

    /* ── Main soul loop ── */
    sched_ctx_t sched;
    scheduler_init(&sched, &soul, quiet);

    static int soul_errors = 0;

    for (int i = 0; rounds < 0 || i < rounds; i++) {
        sched.rounds_since_mod++;

        int rc = soul_read(&soul);
        if (rc != 0) soul_errors++;
        else soul_errors = 0;
        if (soul_errors > 10 && soul_errors % 100 == 0)
            fprintf(stderr, "[TORK] WARNING: %d consecutive soul_read failures\n", soul_errors);
        if (rc != 0) {
            fprintf(stderr, "[%4d] soul_read failed (rc=%d) — core died?\n", i, rc);
            break;
        }

        /* Build instinct input */
        instinct_input_t inp = {
            .tick     = soul_tick(&soul),
            .elapsed  = soul_elapsed(&soul),
            .expected = soul_expected(&soul),
            .hw_stress = soul_hw_stress(&soul),
            .mode     = soul_mode(&soul),
            .code_insns = soul_code_insns(&soul),
            .code_ctrl  = soul_code_ctrl(&soul),
            .code_mod_success = soul_code_mod_success(&soul),
            .code_opt_saved   = soul_code_opt_saved(&soul),
            .code_nop_count   = soul_code_nop_count(&soul),
            .fission_count    = soul_fission_count(&soul),
            .wins             = soul_wins(&soul),
            .bb_global_opts   = bb_global_optimizations(),
            .params           = NULL,
            .active_rules     = ind_active_count(),
            .rule_applied     = 0,
            .restored_files   = restored_files,
            .save_success     = 0,
            .idle_discoveries = 0,
            .pattern_best_action = -1,
            .pattern_confidence  = 0.0f,
            .env_changed       = 0,
        };

        /* Evaluate instinct */
        tork_instinct_t inst = instinct_evaluate(&inp);
        int drive = (int)((inst.desire - inst.fear + inst.curiosity) * 100.0f);
        if (drive > 127) drive = 127;
        if (drive < -128) drive = -128;
        soul_set_drive(&soul, (int8_t)drive);

        /* Pattern query */
        {
            float pat_conf = 0.0f;
            int8_t prev_drive = soul_drive(&soul);
            int pat_action = pat_query_best_action(inp.hw_stress,
                prev_drive, inp.fission_count, &pat_conf);
            if (pat_action >= 0 && pat_conf > 0.0f) {
                inp.pattern_best_action = pat_action;
                inp.pattern_confidence  = pat_conf;
                if (!quiet) printf("  PATTERN: action=%d conf=%.3f (drive=%d)\n", pat_action, pat_conf, (int)prev_drive);
            }
        }

        /* Fill scheduler context for this tick */
        sched.round = i;
        sched.inp = inp;
        sched.inst = inst;
        sched.drive = drive;

        /* Single entry point for all periodic tasks */
        scheduler_tick(&sched);

        /* Sync back any changes scheduler made */
        drive = sched.drive;
        inp = sched.inp;

        /* 大脑改写心跳常量：将决策节奏同步到ASM心跳 */
        {
            uint16_t hb = (uint16_t)tune_get_params().heartbeat_interval;
            int rc = soul_set_heartbeat_ms(&soul, hb);
            if (rc != 0 && i < 3) {
                fprintf(stderr, "WARN: soul_set_heartbeat_ms(%u) failed (rc=%d)\n", hb, rc);
            }
        }

        usleep(tune_get_params().heartbeat_interval * 1000);
    }

    /* ── Shutdown ── */
    printf("\nshutting down core (pid %d)...\n", core_pid);
    kill(core_pid, SIGTERM);
    int st;
    waitpid(core_pid, &st, 0);
    printf("core exited.\n");

    ps_save_all(soul.buf, SOUL_SIZE);
    ps_cleanup_baks();
    fission_cleanup();
    bb_cleanup();
    self_cal_save();
    ind_cleanup();
    soul_close(&soul);
    return 0;
}

int soul_verify(soul_t *s) {
    uint8_t tmp[SOUL_SIZE];
    memcpy(tmp, s->buf, SOUL_SIZE);
    uint32_t saved_crc;
    memcpy(&saved_crc, tmp + S_CRC, 4);
    memset(tmp + S_CRC, 0, 4);
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < SOUL_SIZE; i++) {
        crc ^= tmp[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
    }
    return (~crc == saved_crc) ? 1 : 0;
}
