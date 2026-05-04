#include "soul_access.h"
#include "instinct.h"
#include "code_reader.h"
#include "code_modifier.h"
#include "monitor.h"
#include "fission.h"
#include "blackboard.h"
#include "calibrator.h"
#include "inductor.h"
#include "persistor.h"
#include "idler.h"
#include "../learning/experience.h"
#include "../learning/mcts.h"
#include "../learning/branch.h"
#include "../learning/pattern.h"
#include "../learning/observer.h"
#include "../learning/snapshot.h"
#include "../learning/energy.h"
#include "../learning/watcher.h"
#include "../learning/self_build.h"
#include "../learning/mutation_guide.h"
#include "torkd.h"
#include "../learning/distributed.h"
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

int main(int argc, char **argv) {
    int rounds = 100;
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--restore") == 0)
            do_restore = 1;
        else if (strcmp(argv[a], "--fresh") == 0)
            do_restore = 0;
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

    /* Initialize shared memory BEFORE restore */
    if (bb_init() != 0)
    exp_init();
    br_init();
    pat_init();
    pat_load();
    obs_init();
    obs_load_baseline();
    snap_init();
    snap_load();
    eng_init();
    watcher_init();
    watcher_load();
    sb_init();
    sb_load();
    mg_init();
    mg_load();
        fprintf(stderr, "warning: bb_init failed — blackboard unavailable\n");
    exp_init();
    br_init();
    pat_init();
    pat_load();
    obs_init();
    obs_load_baseline();
    snap_init();
    snap_load();
    eng_init();
    watcher_init();
    watcher_load();
    sb_init();
    sb_load();
    mg_init();
    mg_load();

    if (cal_init() != 0)
        fprintf(stderr, "warning: cal_init failed — calibrator unavailable\n");

    if (ind_init() != 0)
        fprintf(stderr, "warning: ind_init failed — inductor unavailable\n");

    if (do_restore) {
        restored_files = ps_restore_all();
        if (restored_files > 0)
            printf("TORK restored from disk (%d files recovered)\n", restored_files);
        else
            printf("TORK restore: no data found, fresh start\n");
    }

    soul_t soul;
    if (soul_open(&soul, core_pid) != 0) {
        fprintf(stderr, "soul_open failed — cannot read /proc/%d/mem\n", core_pid);
        kill(core_pid, SIGTERM);
        return 1;
    }

        /* Restore soul data into live process if available */
    if (do_restore) {
        uint8_t soul_saved[SOUL_SIZE];
        size_t soul_got = ps_restore_soul(soul_saved, SOUL_SIZE);
        if (soul_got == SOUL_SIZE) {
            /* Write tick + drive back into core so it resumes where it left off */
            uint32_t saved_tick;
            memcpy(&saved_tick, soul_saved, 4);
            soul_write_buf(&soul, S_TICK, &saved_tick, 4);
            restored_files++;
            printf("TORK restored soul: resuming from tick %u\n", saved_tick);
        }
    }

    /* ── 启动集成 Socket 服务 ── */
    if (torkd_init(&soul) == 0) {
        printf("  TORKD: socket ready at %s\n", TORKD_SOCKET_PATH);
    } else {
        printf("  TORKD: socket init failed (non-fatal)\n");
    }

    /* ── 启动分布式黑板（UDP 多播） ── */
    if (dist_init() == 0) {
        /* success, message printed by dist_init */
    } else {
        printf("  DIST: network unavailable (non-fatal, running solo)\n");
    }

    /* ── 启动网格 Soul 共享内存 ── */
    if (grid_engine_init() == 0) {
        printf("  GRID: soul shared memory ready at /dev/shm/tork_soul.bin\n");
    } else {
        printf("  GRID: shared memory init failed (non-fatal)\n");
    }

    /* write self_pid and ppid once */
    {
        uint32_t pid_val = (uint32_t)core_pid;
        soul_write_buf(&soul, S_SELF_PID, &pid_val, 4);
    }
    {
        uint32_t val;
        if (monitor_parse_proc_status(core_pid, "PPid:\t", &val) == 0) {
            uint16_t v = (val > 65535) ? 65535 : (uint16_t)val;
            soul_write_buf(&soul, S_PPID, &v, 2);
        }
    }

    
    /* write agreement status and sandbox level into soul */
    {
        uint8_t agreed = 0;
        uint8_t sandbox_level = 0;
        /* Try to read from /etc/tork/agreement.sig */
        FILE *agf = fopen("/etc/tork/agreement.sig", "rb");
        if (agf) {
            uint8_t agbuf[68];
            if (fread(agbuf, 1, 68, agf) == 68) {
                uint32_t magic;
                memcpy(&magic, agbuf, 4);
                if (magic == 0x4B524F54) {
                    agreed = agbuf[8];  /* state field */
                    sandbox_level = agbuf[12];  /* sandbox field */
                }
            }
            fclose(agf);
        }
        soul_write_byte(&soul, 0x48, agreed);        /* S_AGREED */
        soul_write_byte(&soul, 0x49, sandbox_level);  /* S_SANDBOX_LEVEL */
        
        if (agreed == 1)
            printf("TORK agreement: ACCEPTED (sandbox level %d)\n", sandbox_level);
        else
            printf("TORK agreement: NONE — limited functionality\n");
    }
    /* v3.0: initialize learning fields */
    {
        uint16_t lr = 500;    /* learning_rate = 0.5 */
        soul_write_buf(&soul, S_LEARNING_RATE, &lr, 2);
        uint16_t cd = 100;    /* curiosity_decay = 0.1 */
        soul_write_buf(&soul, S_CURIOSITY_DECAY, &cd, 2);
        uint32_t exp_count_init = exp_count();
        soul_write_buf(&soul, S_EXPERIENCE_COUNT, &exp_count_init, 4);
        soul_write_buf(&soul, S_EXPERIENCE_SAVED, &exp_count_init, 4);
    }
printf("TORK engine started. core PID=%d\n", core_pid);
    printf("TORK v2.0 | generation data at 0x54 | learn_count at 0x4C\n");
    printf("polling 500ms | instinct 10 | code 200 | modify 300 | optimize 600 | nop 900 | fission 1000 | bb 100 | cal 500 | ind 800 | persist 1000\n\n");

    int mod_attempted = 0;
    int opt_attempted = 0;
    int nop_attempted = 0;
    int rounds_since_mod = 0;
            /* v2.2: self-awareness counter */
            static int total_rounds = 0; total_rounds++;
    uint32_t last_bb_tick = 1;
    uint32_t prev_bb_opt_at_idle = 0;  /* non-zero to avoid (tick-0)<200 always true */
    int idle_discoveries = 0;
    /* Experience feedback tracking */
    int feedback_pending = 0;
    int feedback_round = 0;
    uint8_t feedback_hw_before = 0;
    int8_t feedback_drive_before = 0;

    for (int i = 0; i < rounds; i++) {
        rounds_since_mod++;
        int rc = soul_read(&soul);
        /* v2.0: tally soul health */
        static int soul_errors = 0;
        if (rc != 0) soul_errors++;
        else soul_errors = 0;
        if (soul_errors > 10 && soul_errors % 100 == 0)
            fprintf(stderr, "[TORK] WARNING: %d consecutive soul_read failures\n", soul_errors);
        if (rc != 0) {
            fprintf(stderr, "[%4d] soul_read failed (rc=%d) — core died?\n", i, rc);
            break;
        }

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
            .params           = cal_params(),
            .active_rules     = ind_active_count(),
            .rule_applied     = 0,
            .restored_files   = restored_files,
            .save_success     = 0,
            .idle_discoveries = idle_discoveries,
        };

        bb_set_tick(inp.tick);

        /* Track blackboard activity for idle detection */
        uint32_t prev_bb_opt = inp.bb_global_opts;
        uint32_t prev_bb_fis = bb_global_fissions();

        const struct tork_params *cp = cal_params();
        int mod_cycle = cp->conservative_cycle * 10;
        int opt_cycle = cp->aggressive_cycle * 10;
        int nop_cycle = cp->nop_cycle * 10;

        tork_instinct_t inst = instinct_evaluate(&inp);

        int drive = (int)((inst.desire - inst.fear + inst.curiosity) * 100.0f);
        if (drive > 127) drive = 127;
        if (drive < -128) drive = -128;
        soul_set_drive(&soul, (int8_t)drive);

        /* ── 网格: 推送 Soul 数据 ── */
        {
            grid_soul_feed_t gs;
            memset(&gs, 0, sizeof(gs));
            gs.tick = inp.tick;
            gs.drive = (int8_t)drive;
            gs.hw_stress = inp.hw_stress;
            gs.gen_count = soul_gen_count ? inp.tick/1000 : 6;
            gs.active_branches = br_active_count();
            gs.experience_count = exp_count();
            gs.energy_mode = 1;
            gs.fear = (uint8_t)(inst.fear * 100);
            gs.desire = (uint8_t)(inst.desire * 100);
            gs.curiosity = (uint8_t)(inst.curiosity * 100);
            
            /* Read recent outcomes from experience */
            gs.outcome_count = 0;
            experience_t recent[16];
            int n = exp_recent(16, recent);
            for (int ei = 0; ei < n && ei < 16; ei++) {
                gs.recent_outcomes[ei] = recent[ei].outcome;
                gs.outcome_count++;
            }
            
            /* Read branch data */
            for (int bi = 0; bi < 8; bi++) {
                gs.branch_drive[bi] = 0;
            }
            
            grid_engine_write(&gs);
        }

        /* ── torkd: 每 tick 处理 socket 客户端 ── */
        torkd_tick();
        /* ── 分布式黑板: 接收/发送经验 ── */
        dist_tick();

        /* every 200 rounds: code reading */
        if (i % 200 == 0) {
            char asm_buf[8192];
            int alen = asm_read_file("benchmark/memcpy/ref.s", asm_buf, sizeof(asm_buf));
            if (alen > 0) {
                int insns = asm_count_insns_in_func(asm_buf, alen, "memcpy_tork");

                char opcodes[32][8];
                asm_extract_opcodes(asm_buf, alen, "memcpy_tork", opcodes, 32);

                int cm = 0, ca = 0, cc = 0, co = 0;
                asm_classify_insns(asm_buf, alen, "memcpy_tork", &cm, &ca, &cc, &co);

                printf("[%4d] tick=%-6u reading memcpy_tork: %d insns\n", i, inp.tick, insns);
                printf("       opcodes:");
                int show = (insns > 10) ? 10 : insns;
                for (int k = 0; k < show; k++) printf(" %s", opcodes[k]);
                printf("\n");
                printf("       class: mov=%d arith=%d control=%d other=%d\n", cm, ca, cc, co);

                uint8_t stats[10];
                memset(stats, 0, sizeof(stats));
                *(uint16_t*)(stats + 0) = (uint16_t)insns;
                *(uint16_t*)(stats + 2) = (uint16_t)cm;
                *(uint16_t*)(stats + 4) = (uint16_t)ca;
                *(uint16_t*)(stats + 6) = (uint16_t)cc;
                *(uint16_t*)(stats + 8) = (uint16_t)co;
                soul_write_buf(&soul, S_CODE_INSNS, stats, 10);

                inp.code_insns = (uint16_t)insns;
                inp.code_ctrl  = (uint16_t)cc;
            }
        }

        /* conservative modification (je→jz) */
        if (i % mod_cycle == 0 && !mod_attempted) {
            char asm_buf[8192];
            int alen = asm_read_file("benchmark/memcpy/ref.s", asm_buf, sizeof(asm_buf));
            if (alen > 0) {
                char backup[8192];
                int backup_len = alen;
                memcpy(backup, asm_buf, alen);

                int rep = asm_replace_operand(asm_buf, alen, "memcpy_tork",
                                              "\tje\t", "\tjz\t", 1);
                if (rep == 1) {
                    int verified = asm_verify_modification(asm_buf, alen, "benchmark/memcpy");
                    if (verified) {
                        FILE *f = fopen("benchmark/memcpy/ref.s", "w");
                        if (f) { fwrite(asm_buf, 1, alen, f); fclose(f); }
                        printf("[%4d] tick=%-6u MODIFY SUCCESS: replaced je with jz\n", i, inp.tick);
                        uint8_t ms = 1;
                        exp_update_last(80, 0, 1, inp.hw_stress, drive);
                        soul_write_buf(&soul, S_CODE_MOD_SUCCESS, &ms, 1);
                        inp.code_mod_success = 1;
                        bb_write(BB_TYPE_OPT_SUCCESS, 1, (uint32_t)i);
                        bb_inc_optimizations();
                        rounds_since_mod = 0;
            /* v2.2: self-awareness counter */
            static int total_rounds = 0; total_rounds++;
                        last_bb_tick = inp.tick;
                    } else {
                        asm_rollback(asm_buf, sizeof(asm_buf), backup, backup_len);
                        printf("[%4d] tick=%-6u MODIFY FAILED: je→jz rejected\n", i, inp.tick);
                        uint8_t ms = 2;
                        exp_update_last(-30, 0, 0, inp.hw_stress, drive);
                        soul_write_buf(&soul, S_CODE_MOD_SUCCESS, &ms, 1);
                        inp.code_mod_success = 2;
                        bb_write(BB_TYPE_OPT_FAIL, 1, (uint32_t)i);
                    }
                } else {
                    printf("[%4d] tick=%-6u MODIFY SKIP: je not found\n", i, inp.tick);
                }
                mod_attempted = 1;
            }
        }

        /* aggressive optimization (dead code deletion) */
        if (i % opt_cycle == 0 && !opt_attempted) {
            char asm_buf[8192];
            int alen = asm_read_file("benchmark/memcpy/ref.s", asm_buf, sizeof(asm_buf));
            if (alen > 0) {
                char backup[8192];
                int backup_len = alen;
                memcpy(backup, asm_buf, alen);

                printf("[%4d] tick=%-6u OPT: scanning for dead code...\n", i, inp.tick);

                int new_len = alen;
                int deleted = asm_delete_dead_insns(asm_buf, alen, "memcpy_tork", &new_len);
                if (deleted > 0) {
                    int verified = asm_verify_modification(asm_buf, new_len, "benchmark/memcpy");
                    if (verified) {
                        FILE *f = fopen("benchmark/memcpy/ref.s", "w");
                        if (f) { fwrite(asm_buf, 1, new_len, f); fclose(f); }
                        printf("[%4d] tick=%-6u OPT: deleted %d dead insn(s) after ret\n",
                               i, inp.tick, deleted);
                        printf("[%4d] tick=%-6u OPT: verification PASSED, writing to file\n",
                               i, inp.tick);
                        exp_update_last(70, 0, 1, inp.hw_stress, drive);
                        uint8_t sv = (uint8_t)deleted;
                        soul_write_buf(&soul, S_CODE_OPT_SAVED, &sv, 1);
                        inp.code_opt_saved = sv;
                        bb_write(BB_TYPE_OPT_SUCCESS, 2, (uint32_t)deleted);
                        bb_inc_optimizations();
                    } else {
                        asm_rollback(asm_buf, sizeof(asm_buf), backup, backup_len);
                        printf("[%4d] tick=%-6u OPT: deleted %d but verification FAILED, rollback\n",
                               i, inp.tick, deleted);
                        exp_update_last(-20, 1, 0, inp.hw_stress, drive);
                    }
                } else {
                    printf("[%4d] tick=%-6u OPT: no dead code found\n", i, inp.tick);
                }
                opt_attempted = 1;
            }
        }

        /* NOP deletion (alignment padding removal) */
        if (i % nop_cycle == 0 && !nop_attempted) {
            char asm_buf[8192];
            int alen = asm_read_file("benchmark/memcpy/ref.s", asm_buf, sizeof(asm_buf));
            if (alen > 0) {
                char backup[8192];
                int backup_len = alen;
                memcpy(backup, asm_buf, alen);

                int new_len = alen;
                int nops = asm_delete_nop_insns(asm_buf, alen, "memcpy_tork", &new_len);
                if (nops > 0) {
                    printf("[%4d] tick=%-6u NOP: found %d nop insns in memcpy_tork\n", i, inp.tick, nops);
                    printf("[%4d] tick=%-6u NOP: deleting %d nop insns...\n", i, inp.tick, nops);
                    int verified = asm_verify_modification(asm_buf, new_len, "benchmark/memcpy");
                    if (verified) {
                        FILE *f = fopen("benchmark/memcpy/ref.s", "w");
                        if (f) { fwrite(asm_buf, 1, new_len, f); fclose(f); }
                        uint8_t total = inp.code_opt_saved + (uint8_t)nops;
                        soul_write_buf(&soul, S_CODE_OPT_SAVED, &total, 1);
                        uint8_t nc = (uint8_t)nops;
                        soul_write_buf(&soul, S_CODE_NOP_COUNT, &nc, 1);
                        inp.code_opt_saved = total;
                        inp.code_nop_count = nc;
                        printf("[%4d] tick=%-6u NOP: verification PASSED, saved %d lines total\n",
                               i, inp.tick, total);
                        bb_write(BB_TYPE_OPT_SUCCESS, 3, (uint32_t)nops);
                        bb_inc_optimizations();
                    } else {
                        asm_rollback(asm_buf, sizeof(asm_buf), backup, backup_len);
                        printf("[%4d] tick=%-6u NOP: deleted %d but verification FAILED, rollback\n",
                               i, inp.tick, nops);
                        exp_update_last(-20, 1, 0, inp.hw_stress, drive);
                    }
                } else if (nops == 0) {
                    printf("[%4d] tick=%-6u NOP: no nop insns found in memcpy_tork\n", i, inp.tick);
                    uint8_t nc = 0;
                    soul_write_buf(&soul, S_CODE_NOP_COUNT, &nc, 1);
                    inp.code_nop_count = 0;
                }
                nop_attempted = 1;
            }
        }

        /* every 1200 rounds: phase 4 — apply an active inductive rule */
        if (i % 1200 == 0 && i > 0) {
            int slot = ind_find_active();
            if (slot >= 0) {
                struct tork_rule all[32];
                ind_load_rules(all, 32);
                struct tork_rule rule = all[slot];

                printf("[%4d] tick=%-6u RULE: applying active rule slot %d "
                       "'%.32s' → '%.32s'\n",
                       i, inp.tick, slot, rule.premise, rule.conclusion);

                int result = ind_test_rule(&rule, "benchmark/memcpy/ref.s");
                ind_update_rule(slot, &rule);

                if (result == 0) {
                    inp.rule_applied = 1;
                    printf("[%4d] tick=%-6u RULE: application PASSED\n", i, inp.tick);
                } else {
                    printf("[%4d] tick=%-6u RULE: application FAILED or not applicable\n", i, inp.tick);
                }
            }
        }

        /* every 1000 rounds: fission check (instinct-driven) */
        if (i % 1000 == 0 && i > 0) {
            if (fission_decide(&soul) && inst.curiosity > 0.6f) {
                printf("[%4d] tick=%-6u FISSION: instinct triggers fission\n", i, inp.tick);
                pid_t child = fission_spawn();
                if (child > 0) {
                    uint16_t cp = (uint16_t)child;
                    soul_write_buf(&soul, S_CHILD_PID, &cp, 2);
                    uint8_t fc = inp.fission_count + 1;
                    soul_write_buf(&soul, S_FISSION_COUNT, &fc, 1);
                    uint16_t ft = (uint16_t)inp.tick;
                    soul_write_buf(&soul, S_FISSION_TICK, &ft, 2);
                    inp.fission_count = fc;
                    printf("[%4d] tick=%-6u FISSION: child spawned PID=%d\n", i, inp.tick, child);
                    bb_write(BB_TYPE_FISSION, 1, (uint32_t)child);
                    bb_inc_fissions();

                    int result = fission_migrate(child);
                    if (result == 0) {
                        /* parent won */
                        uint16_t w = inp.wins + 1;
                        soul_write_buf(&soul, S_WINS, &w, 2);
                        inp.wins = w;
                        /* clear child_pid */
                        uint16_t zero16 = 0;
                        soul_write_buf(&soul, S_CHILD_PID, &zero16, 2);
                        printf("[%4d] tick=%-6u FISSION: parent won, wins=%d\n", i, inp.tick, w);
                    }
                    /* if child won, fission_migrate already called exit(0) */
                } else {
                    printf("[%4d] tick=%-6u FISSION: spawn failed\n", i, inp.tick);
                }
            } else {
                printf("[%4d] tick=%-6u FISSION: conditions not met (tick=%u drive=%+d curiosity=%.1f)\n",
                       i, inp.tick, inp.tick, drive, inst.curiosity);
            }
        }

        /* every 500 rounds: calibrator self-calibration */
        if (i % 500 == 0 && i > 0) {
            cal_extract_patterns();
            struct tork_params suggested;
            if (cal_suggest_adjustments(&suggested) == 0) {
                cal_apply_adjustments(&suggested);
            }
        }

        /* every 800 rounds: inductive learning — extract & generalize */
        if (i % 800 == 0 && i > 0) {
            struct tork_rule experiences[8];
            int n = ind_extract_experiences(experiences, 8);
            if (n >= 2) {
                struct tork_rule generalized;
                if (ind_generalize(experiences, n, &generalized) == 0) {
                    int slot = ind_save_rule(&generalized);
                    printf("[%4d] IND: generalized rule from %d experiences → slot %d "
                           "premise='%.32s' confidence=%d%%\n",
                           i, n, slot, generalized.premise, generalized.confidence);
                }
            } else if (n == 1) {
                /* Single experience — save as candidate rule */
                int slot = ind_save_rule(&experiences[0]);
                if (slot >= 0)
                    printf("[%4d] IND: extracted 1 experience → slot %d\n", i, slot);
            }
        }

        /* every 1000 rounds: test a pending rule */
        if (i % 1000 == 0 && i > 0) {
            int slot = ind_find_pending();
            if (slot >= 0) {
                struct tork_rule rule;
                ind_load_rules(&rule, 1);  /* load just this one */
                /* Read the correct slot */
                struct tork_rule all[32];
                ind_load_rules(all, 32);
                rule = all[slot];

                int result = ind_test_rule(&rule, "benchmark/memcpy/ref.s");
                ind_update_rule(slot, &rule);
                printf("[%4d] IND: tested rule slot %d → %s confidence=%d%% active=%d\n",
                       i, slot, result == 0 ? "PASS" : "FAIL",
                       rule.confidence, rule.active);
            }
        }

        /* every 1000 rounds: persist soul + blackboard */
        if (i % 1000 == 0 && i > 0) {
            if (ps_save_all(soul.buf, SOUL_SIZE) == 0) {
                inp.save_success = 1;
                printf("[%4d] tick=%-6u PERSIST: state saved to disk\n", i, inp.tick);
            }
        }

        /* every 5000 rounds: persist params + rules (full save) + decay */
        if (i % 5000 == 0 && i > 0) {
            if (ps_save_all(soul.buf, SOUL_SIZE) == 0) {
                inp.save_success = 1;
                printf("[%4d] tick=%-6u PERSIST: full save (params+rules)\n", i, inp.tick);
            }
            ps_decay_memory();
        }

        /* every 10000 rounds: deep decay */
        if (i % 10000 == 0 && i > 0) {
            ps_decay_memory();
        }

        /* re-evaluate instinct after all modifications */
        inst = instinct_evaluate(&inp);
        idle_discoveries = 0;  /* one-shot: clear after instinct consumes it */
        drive = (int)((inst.desire - inst.fear + inst.curiosity) * 100.0f);
        if (drive > 127) drive = 127;
        if (drive < -128) drive = -128;
        soul_set_drive(&soul, (int8_t)drive);
        
        /* Check branch-fork conditions */
        {
            fork_request_t fork_req;
            fork_req.should_fork = 0;
            fork_req.current_tick = inp.tick;
            fork_req.gen_count = soul_gen_count(&soul);
            fork_req.drive = drive;
            fork_req.sandbox_level = inp.mode;  /* Use mode as proxy */
            fork_req.branch_cool_tick = br_last_fork_tick();
            
            if (br_should_fork(&fork_req)) {
                int slot = br_fork(&soul, inp.tick, soul_gen_count(&soul));
                if (slot >= 0) {
                    printf("[%4d] tick=%-6u BRANCH: fork created at slot %d (drive=%d)\n",
                           i, inp.tick, slot, drive);
                }
            }
        }

        /* Advance all active branches */
        br_advance_all();
        
        /* Observer: sample system state every OBS_SAMPLE_INTERVAL ticks */
        if (inp.tick % OBS_SAMPLE_INTERVAL == 0) {
            obs_sample(inp.tick, inp.hw_stress, (int8_t)drive, 47, 30, 
                       (uint16_t)br_active_count());
            int anomaly = obs_check_anomaly(inp.hw_stress, (int8_t)drive, 47, 30);
            if (anomaly > 1) {
                printf("[%4d] tick=%-6u OBS: anomaly detected (flags=%d)\n",
                       i, inp.tick, anomaly);
            }
        }
        
        /* Energy: update stats and auto-adjust */
        {
            float hb_mult = eng_auto_adjust(inp.tick, inp.hw_stress);
            eng_update(30.0f, 4096.0f, 0.1f, (drive == 0));
            if (hb_mult != 1.0f && (i % 10 == 0)) {
                printf("[%4d] tick=%-6u ENG: heartbeat mult=%.1f\n",
                       i, inp.tick, hb_mult);
            }
            /* Limit branches if energy says so */
            if (eng_should_limit_branches() && br_active_count() > 2) {
                /* Don't fork new branches when system is loaded */
            }
        }
        
        /* Watcher: scan /proc every 10 ticks */
        if (i > 0 && i % 10 == 0) {
            watcher_scan_proc();
        }
        /* Learn patterns every 100 ticks */
        if (i > 0 && i % 100 == 0) {
            int new_pats = watcher_learn_patterns();
            if (new_pats > 0) {
                printf("[%4d] tick=%-6u WATCH: learned %d new patterns\n",
                       i, inp.tick, new_pats);
            }
        }
        /* Self-build: check sources every 50 ticks */
        if (i > 0 && i % 50 == 0) {
            int changed = sb_check_sources();
            if (changed > 0) {
                printf("[%4d] tick=%-6u SELF: %d source files changed\n",
                       i, inp.tick, changed);
            }
        }
        /* Mutation guide: recommend evolution direction every 200 ticks */
        if (i > 0 && i % 200 == 0 && drive > 0) {
            char rec[128];
            mg_strategy_type_t s = mg_recommend(rec, sizeof(rec));
            (void)s;
            if (i % 1000 == 0) {
                printf("[%4d] tick=%-6u MGUIDE: recommend %s\n",
                       i, inp.tick, rec);
            }
        }
        
        /* Snapshot: auto-save healthy state */
        {
            uint8_t soul_raw[SOUL_SIZE];
            memset(soul_raw, 0, 128);
            memcpy(soul_raw, &soul, sizeof(soul));
            snap_auto(inp.tick, (int64_t)drive, inp.hw_stress,
                      soul_gen_count(&soul), soul_raw);
        }
        
        /* Update instinct input with branch status */
        inp.branch_active_count = br_active_count();
        
        /* Health check: detect degradation, auto-rollback via /proc/PID/mem */
        {
            health_check_t hc = snap_health_check(inp.tick, (int64_t)drive,
                                                   inp.hw_stress, 1);
            /* Commit: if stable for a long time, mark state as good */
            if (!hc.degraded && inp.tick > 0 && 
                inp.tick % (SNAP_AUTO_INTERVAL * 8) == 0) {
                uint8_t soul_raw[SOUL_SIZE];
                memset(soul_raw, 0, SOUL_SIZE);
                memcpy(soul_raw, &soul, sizeof(soul));
                snap_commit(inp.tick, (int64_t)drive, inp.hw_stress,
                           soul_gen_count(&soul), soul_raw);
            }
            
            if (hc.degraded && core_pid > 0) {
                printf("[%4d] tick=%-6u SNAP: DEGRADED (drive_drop=%.0f), ROLLING BACK\n",
                       i, inp.tick, hc.drive_drop);
                
                uint8_t restored_soul[SOUL_SIZE];
                uint32_t restore_tick = 0;
                int r = snap_rollback(restored_soul, &restore_tick);
                if (r > 0 && restore_tick > 0) {
                    /* Write restored soul to core process via soul.wr_fd */
                    if (soul.wr_fd >= 0) {
                        lseek(soul.wr_fd, SOUL_ADDR_VAL, SEEK_SET);
                        ssize_t written = write(soul.wr_fd, restored_soul, SOUL_SIZE);
                        printf("[%4d] tick=%-6u SNAP: wrote %zd bytes to core @ 0x%x\n",
                               i, inp.tick, written, SOUL_ADDR_VAL);
                        
                        /* Re-read to verify rollback */
                        soul_read(&soul);
                    } else {
                        printf("[%4d] tick=%-6u SNAP: no wr_fd, cannot rollback\n",
                               i, inp.tick);
                    }
                }
            }
        }
        {
            uint32_t lft = br_last_fork_tick();
            inp.branch_fork_ticks_ago = (lft > 0) ? (int)(inp.tick - lft) : -1;
        }
        {
            reap_report_t rr = br_last_reap();
            inp.branch_reap_just_happened = (rr.death_reason != 0 && rr.branch_id > 0);
        }
        
        /* Update last_bb_tick if blackboard changed this round */
        {
            uint32_t cur_bb_opt = bb_global_optimizations();
            uint32_t cur_bb_fis = bb_global_fissions();
            if (cur_bb_opt != prev_bb_opt || cur_bb_fis != prev_bb_fis)
                last_bb_tick = inp.tick;
        }
        
        /* Experience feedback: evaluate outcome 50 rounds after idle */
        if (feedback_pending && i - feedback_round >= 50) {
            int8_t outcome = 0;
            uint8_t hw_change = (inp.hw_stress < feedback_hw_before) ? 1 : 
                                (inp.hw_stress > feedback_hw_before) ? 2 : 0;
            int8_t drive_change = drive - feedback_drive_before;
            
            /* Positive: stress decreased or drive increased */
            if (hw_change == 1) outcome += 20;
            else if (hw_change == 2) outcome -= 15;
            if (drive_change > 10) outcome += 30;
            else if (drive_change > 0) outcome += 10;
            else if (drive_change < -10) outcome -= 20;
            else if (drive_change < 0) outcome -= 5;
            
            /* Check if any optimization happened */
            uint32_t opt_delta = bb_global_optimizations() - prev_bb_opt_at_idle;
            if (opt_delta > 0) outcome += 25;
            
            exp_update_last(outcome, 0, 0, inp.hw_stress, drive);
            printf("[%4d] tick=%-6u FB: outcome=%d (hw=%d→%d, drive=%d→%d, opts=%u)\n",
                   i, inp.tick, outcome, feedback_hw_before, inp.hw_stress,
                   feedback_drive_before, drive, opt_delta);
            
            feedback_pending = 0;
        }

        /* every 100 rounds: idle check */
        if (i % 100 == 0 && i > 0) {
            idler_input_t idle_in = {
                .hw_stress       = inp.hw_stress,
                .drive           = drive,
                .gen_count       = soul_gen_count(&soul),
                .tick            = inp.tick,
                .last_bb_tick    = last_bb_tick,
                .rounds_since_mod = rounds_since_mod,
            };
            if (idler_should_enter(&idle_in)) {
                if (!idler_active()) {
                    printf("[%4d] tick=%-6u IDLE: entering idle (stress=%d drive=%d gen=%u)\n",
                           i, inp.tick, inp.hw_stress, drive, soul_gen_count(&soul));
                    uint8_t mode_idle = 1;
                    soul_write_buf(&soul, S_MODE, &mode_idle, 1);
                    idler_set_active(1);
                    idler_output_t idle_out = idler_cycle(&idle_in);
                    int disc = idle_out.discoveries;
                    if (disc > 0) idle_discoveries += disc;
                    else if (disc == 0) idle_discoveries = -1;
                    printf("[%4d] tick=%-6u IDLE: exiting idle (action=%d, discoveries=%d)\n",
                           i, inp.tick, idle_out.action_type, disc);
                    uint8_t mode_busy = 0;
                    soul_write_buf(&soul, S_MODE, &mode_busy, 1);
                    /* Mark feedback pending — will evaluate after 50 rounds */
                    feedback_pending = 1;
                    feedback_round = i;
                    feedback_hw_before = inp.hw_stress;
                    prev_bb_opt_at_idle = bb_global_optimizations();
                    feedback_drive_before = drive;
                    idler_set_active(0);
                }
            } else if (idler_active()) {
                printf("[%4d] tick=%-6u IDLE: conditions changed, forced exit\n", i, inp.tick);
                uint8_t mode_busy = 0;
                soul_write_buf(&soul, S_MODE, &mode_busy, 1);
                idler_set_active(0);
            }
        }

        /* every 100 rounds: blackboard summary */
        if (i % 100 == 0) {
            uint32_t bb_opt = bb_global_optimizations();
            uint32_t bb_fis = bb_global_fissions();
            uint32_t bb_err = bb_global_errors();
            printf("[%4d] tick=%-6u BB: opts=%u fissions=%u errors=%u\n",
                   i, inp.tick, bb_opt, bb_fis, bb_err);
        }

        /* every 10 rounds: print state */
        if (i % 10 == 0) {
            uint32_t bb_opt = bb_global_optimizations();
            printf("[%4d] tick=%-6u pid=%-5u ppid=%-4u drive=%+4d fear=%.1f desire=%.1f curiosity=%.1f fission=%d wins=%d bb_opts=%u\n",
                   i, inp.tick, soul_self_pid(&soul), soul_ppid(&soul),
                   drive, inst.fear, inst.desire, inst.curiosity,
                   inp.fission_count, inp.wins, bb_opt);
        }

        usleep(500000);
    }

    printf("\nshutting down core (pid %d)...\n", core_pid);
    kill(core_pid, SIGTERM);
    int st;
    waitpid(core_pid, &st, 0);
    printf("core exited.\n");

    ps_save_all(soul.buf, SOUL_SIZE);
    ps_cleanup_baks();
    fission_cleanup();
    bb_cleanup();
    cal_cleanup();
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