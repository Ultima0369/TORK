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
#include "beacon.h"
#include "fractal.h"
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
// TORK_EVOLVE: engine_include_insert
#include <sys/file.h>

static pid_t core_pid = 0;
static int do_restore = 0;
static int restored_files = 0;
static int golden_exists = 0;           /* 不可变：一旦生成，不再自动覆盖 */
static int crc_fail_count = 0;          /* CRC 连续失败计数 */
static int golden_observe_remaining = 0;/* 恢复后500拍内禁止变异 */

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

    static peer_table_t beacon_peers;
    if (beacon_init(&beacon_peers) == 0)
        printf("  BEACON: peer discovery ready on port %d\n", BEACON_PORT);
    else
        printf("  BEACON: init failed (non-fatal)\n");

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
    {
        /* S_NODE_ID: RDRAND + TSC + PID 生成唯一节点标识 */
        uint8_t node_id[16];
        memset(node_id, 0, 16);
        uint64_t tsc;
        __asm__ __volatile__("rdtsc" : "=A"(tsc));
        memcpy(node_id, &tsc, 8);
        uint32_t pid_val = (uint32_t)core_pid;
        memcpy(node_id + 8, &pid_val, 4);
        /* 尝试 RDRAND 增加熵 */
        unsigned int rnd;
        __asm__ __volatile__("1: rdrand %0; jnc 1b" : "=r"(rnd));
        memcpy(node_id + 12, &rnd, 4);
        soul_write_buf(soul, S_NODE_ID, node_id, 16);
    }
}

/* ── CRC self-check with 3-strike fuse ── */
static void check_soul_crc(soul_t *soul, int tick) {
    soul_update_crc(soul);
    if (tick <= 0 || tick % 100 != 0) return;

    uint32_t computed = soul_compute_crc(soul);
    uint32_t saved;
    memcpy(&saved, soul->buf + S_CRC, 4);
    float crc_input = (computed == saved) ? 0.0f : 1.0f;
    float crc_ref   = 0.0f;
    float crc_tol   = 0.33f;
    float crc_wt    = 1.0f;
    fractal_input_t finp = {
        .dims = 1, .input = &crc_input, .reference = &crc_ref,
        .tolerance = &crc_tol, .weights = &crc_wt
    };
    fractal_output_t fout = fractal_step(&finp, NULL, NULL);
    if (fout.delta > 0.0f) {
        crc_fail_count++;
        fprintf(stderr, "[%4d] SOUL CRC MISMATCH (consecutive: %d/3, conf=%.2f)\n",
                tick, crc_fail_count, fout.confidence);
        if (crc_fail_count >= 3) {
            fprintf(stderr, "[%4d] SOUL: 3 consecutive CRC failures — restoring golden backup\n", tick);
            if (soul_restore_golden(soul, core_pid) == 0)
                printf("[%4d] SOUL: golden restore succeeded — entering 500-tick observation\n", tick);
            else
                fprintf(stderr, "[%4d] SOUL: golden restore FAILED — awaiting external intervention\n", tick);
        }
    } else {
        crc_fail_count = 0;
    }
}

/* ── Build instinct input from soul fields ── */
static instinct_input_t build_instinct_input(soul_t *soul) {
    instinct_input_t inp = {
        .tick     = soul_tick(soul),
        .elapsed  = soul_elapsed(soul),
        .expected = soul_expected(soul),
        .hw_stress = soul_hw_stress(soul),
        .mode     = soul_mode(soul),
        .code_insns = soul_code_insns(soul),
        .code_ctrl  = soul_code_ctrl(soul),
        .code_mod_success = soul_code_mod_success(soul),
        .code_opt_saved   = soul_code_opt_saved(soul),
        .code_nop_count   = soul_code_nop_count(soul),
        .fission_count    = soul_fission_count(soul),
        .wins             = soul_wins(soul),
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
    return inp;
}

/* ── Compute drive from instinct ── */
static int compute_drive(tork_instinct_t *inst) {
    int drive = (int)((inst->desire - inst->fear + inst->curiosity) * 100.0f);
    if (drive > 127) drive = 127;
    if (drive < -128) drive = -128;
    return drive;
}

/* ── Pattern query ── */
static void query_pattern(soul_t *soul, instinct_input_t *inp, int quiet) {
    float pat_conf = 0.0f;
    int8_t prev_drive = soul_drive(soul);
    int pat_action = pat_query_best_action(inp->hw_stress,
        prev_drive, inp->fission_count, &pat_conf);
    if (pat_action >= 0 && pat_conf > 0.0f) {
        inp->pattern_best_action = pat_action;
        inp->pattern_confidence  = pat_conf;
        if (!quiet) printf("  PATTERN: action=%d conf=%.3f (drive=%d)\n", pat_action, pat_conf, (int)prev_drive);
    }
}

/* ── Sync heartbeat interval to soul ── */
static void sync_heartbeat(soul_t *soul, int tick) {
    uint16_t hb = (uint16_t)tune_get_params().heartbeat_interval;
    int rc = soul_set_heartbeat_ms(soul, hb);
    if (rc != 0 && tick < 3) {
        fprintf(stderr, "WARN: soul_set_heartbeat_ms(%u) failed (rc=%d)\n", hb, rc);
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
    printf("TORK v3.17 | π-heartbeat | generation at 0x54 | learn at 0x4C\n");
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

        check_soul_crc(&soul, i);

        if (!golden_exists && i > 0 && i % 10 == 0)
            soul_save_golden(&soul, core_pid);

        if (golden_observe_remaining > 0)
            golden_observe_remaining--;

        instinct_input_t inp = build_instinct_input(&soul);
        tork_instinct_t inst = instinct_evaluate(&inp);
        int drive = compute_drive(&inst);
        soul_set_drive(&soul, (int8_t)drive);

        query_pattern(&soul, &inp, quiet);

        sched.round = i;
        sched.inp = inp;
        sched.inst = inst;
        sched.golden_observe_remaining = golden_observe_remaining;
        sched.drive = drive;

        scheduler_tick(&sched);

        drive = sched.drive;
        inp = sched.inp;

        sync_heartbeat(&soul, i);

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
    beacon_shutdown();
    soul_close(&soul);
    return 0;
}

/* ── CRC32 计算 (多项式 0xEDB88320) ────────────────────────── */

static uint32_t soul_crc32_raw(const uint8_t *buf, int len) {
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
    }
    return ~crc;
}

uint32_t soul_compute_crc(const soul_t *s) {
    uint8_t tmp[SOUL_SIZE];
    memcpy(tmp, s->buf, SOUL_SIZE);
    memset(tmp + S_CRC, 0, 4);
    return soul_crc32_raw(tmp, SOUL_SIZE);
}

void soul_update_crc(soul_t *s) {
    uint32_t crc = soul_compute_crc(s);
    memcpy(s->buf + S_CRC, &crc, 4);
    soul_write_buf(s, S_CRC, &crc, 4);
}

int soul_verify_crc(const soul_t *s) {
    uint32_t computed = soul_compute_crc(s);
    uint32_t saved;
    memcpy(&saved, s->buf + S_CRC, 4);
    return (computed == saved) ? 1 : 0;
}

int soul_verify(soul_t *s) {
    return soul_verify_crc(s);
}

/* ── 黄金备份 ─────────────────────────────────────────────── */

#define GOLDEN_DIR  "persist"
#define GOLDEN_PATH "persist/soul_golden.bin"
#define GOLDEN_LOCK "persist/soul_golden.lock"

/* ── 黄金备份锁文件 ─────────────────────────────────────── */
static int golden_lock_fd = -1;

static int golden_lock(void) {
    golden_lock_fd = open(GOLDEN_LOCK, O_WRONLY | O_CREAT, 0600);
    if (golden_lock_fd < 0) return -1;
    if (flock(golden_lock_fd, LOCK_EX | LOCK_NB) != 0) {
        close(golden_lock_fd);
        golden_lock_fd = -1;
        return -1;
    }
    return 0;
}

static void golden_unlock(void) {
    if (golden_lock_fd >= 0) {
        flock(golden_lock_fd, LOCK_UN);
        close(golden_lock_fd);
        golden_lock_fd = -1;
    }
}

/* ── 黄金备份健康检查 ───────────────────────────────────── */
static int golden_health_ok(soul_t *s) {
    int8_t drive = (int8_t)s->buf[S_DRIVE];
    if (drive < -10 || drive > 10) return 0;
    if (s->buf[S_HW_STRESS] != 0) return 0;
    if (!soul_verify_crc(s)) return 0;
    return 1;
}

int soul_save_golden(soul_t *s, pid_t core_pid) {
    /* 不可变原则：黄金备份一旦存在，不再自动覆盖 */
    if (golden_exists) return 0;

    /* 健康门槛：drive[-10,10], stress=0, CRC通过 */
    if (!golden_health_ok(s)) return -1;

    /* 锁文件 */
    if (golden_lock() != 0) return -1;

    /* 双重确认 tick 稳定 — 在 ptrace 之前读取 */
    soul_read(s);
    uint32_t tick1;
    memcpy(&tick1, s->buf + S_TICK, 4);
    usleep(1000);
    soul_read(s);
    uint32_t tick2;
    memcpy(&tick2, s->buf + S_TICK, 4);
    if (tick1 != tick2) { golden_unlock(); return -1; }

    /* tick 稳定后 ptrace 锁定 */
    if (ptrace(PTRACE_ATTACH, core_pid, NULL, NULL) != 0) {
        golden_unlock(); return -1;
    }
    waitpid(core_pid, NULL, 0);

    /* 读取完整灵魂快照 */
    soul_read(s);

    /* 释放 */
    ptrace(PTRACE_DETACH, core_pid, NULL, NULL);

    /* 计算 CRC 并填入 */
    soul_update_crc(s);

    /* 存盘 + 回读验证 (最多3次) */
    for (int attempt = 0; attempt < 3; attempt++) {
        char tmp_path[256];
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", GOLDEN_PATH);
        int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) continue;
        ssize_t w = write(fd, s->buf, SOUL_SIZE);
        fsync(fd);
        close(fd);
        if (w != SOUL_SIZE) { unlink(tmp_path); continue; }
        if (rename(tmp_path, GOLDEN_PATH) != 0) { unlink(tmp_path); continue; }

        /* 回读验证 */
        uint8_t verify_buf[SOUL_SIZE];
        fd = open(GOLDEN_PATH, O_RDONLY);
        if (fd < 0) continue;
        ssize_t r = read(fd, verify_buf, SOUL_SIZE);
        close(fd);
        if (r != SOUL_SIZE) continue;
        if (memcmp(s->buf, verify_buf, SOUL_SIZE) == 0) {
            golden_exists = 1;
            golden_unlock();
            return 0;
        }
    }
    golden_unlock();
    return -1;
}

int soul_restore_golden(soul_t *s, pid_t core_pid) {
    /* 锁文件 */
    if (golden_lock() != 0) return -1;

    /* 1. 读盘 + CRC 校验 */
    uint8_t golden_buf[SOUL_SIZE];
    int fd = open(GOLDEN_PATH, O_RDONLY);
    if (fd < 0) { golden_unlock(); return -1; }
    ssize_t r = read(fd, golden_buf, SOUL_SIZE);
    close(fd);
    if (r != SOUL_SIZE) { golden_unlock(); return -1; }

    /* CRC 校验 */
    uint32_t saved_crc;
    memcpy(&saved_crc, golden_buf + S_CRC, 4);
    memset(golden_buf + S_CRC, 0, 4);
    uint32_t computed_crc = soul_crc32_raw(golden_buf, SOUL_SIZE);
    if (computed_crc != saved_crc) {
        fprintf(stderr, "SOUL: golden backup CRC mismatch — aborting restore\n");
        golden_unlock();
        return -1;
    }

    /* 2. ptrace 锁定 */
    if (ptrace(PTRACE_ATTACH, core_pid, NULL, NULL) != 0) {
        golden_unlock(); return -1;
    }
    waitpid(core_pid, NULL, 0);

    /* 3. 逐字节回写 + 逐字节回读验证（核心已被 ptrace 挂起，用 _locked 变体）
       每字节最多重试3次 */
    for (int off = 0; off < SOUL_SIZE; off++) {
        /* 跳过 CRC 字段本身 (0x28-0x2B) — 最后单独写 */
        if (off >= S_CRC && off < S_CRC + 4) continue;

        uint8_t val = golden_buf[off];
        int byte_ok = 0;
        for (int retry = 0; retry < 3; retry++) {
            if (soul_write_byte_locked(s, (uint32_t)off, val) != 0) break;
            /* 回读验证 */
            if (lseek(s->mem_fd, SOUL_ADDR_VAL + off, SEEK_SET) != (off_t)-1) {
                uint8_t rb;
                if (read(s->mem_fd, &rb, 1) == 1 && rb == val) {
                    byte_ok = 1;
                    break;
                }
            }
        }
        if (!byte_ok) {
            fprintf(stderr, "SOUL: restore verify fail at offset 0x%02X after 3 retries\n", off);
            ptrace(PTRACE_DETACH, core_pid, NULL, NULL);
            golden_unlock();
            return -1;
        }
    }

    /* 4. 重置：drive=0, hw_stress=0, mode=0 */
    uint8_t zero8 = 0;
    soul_write_byte_locked(s, S_DRIVE, zero8);
    soul_write_byte_locked(s, S_HW_STRESS, zero8);
    soul_write_byte_locked(s, S_MODE, zero8);

    /* 5. TLN 强制悬置：所有 hint 归零 */
    soul_write_byte_locked(s, S_TLN_ACTION, zero8);
    soul_write_byte_locked(s, S_TLN_MODIFY, zero8);
    soul_write_byte_locked(s, S_TLN_EXPLORE, zero8);
    soul_write_byte_locked(s, S_TLN_ENERGY, zero8);

    /* 写入 CRC */
    uint32_t crc = soul_crc32_raw(golden_buf, SOUL_SIZE);
    soul_write_buf_locked(s, S_CRC, &crc, 4);

    /* 6. 释放 */
    ptrace(PTRACE_DETACH, core_pid, NULL, NULL);

    /* 同步本地缓存 */
    memcpy(s->buf, golden_buf, SOUL_SIZE);
    s->buf[S_DRIVE] = 0;
    s->buf[S_HW_STRESS] = 0;
    s->buf[S_MODE] = 0;
    s->buf[S_TLN_ACTION] = 0;
    s->buf[S_TLN_MODIFY] = 0;
    s->buf[S_TLN_EXPLORE] = 0;
    s->buf[S_TLN_ENERGY] = 0;
    memcpy(s->buf + S_CRC, &crc, 4);

    /* 7. 记录经验：action_type=7 (golden_restore) */
    uint32_t cur_tick;
    memcpy(&cur_tick, s->buf + S_TICK, 4);
    exp_record(cur_tick, 0, 0, 0, 7, 0, 100, 0, 0, 0, 0);

    /* 8. 进入500拍观察期 */
    golden_observe_remaining = 500;
    crc_fail_count = 0;

    golden_unlock();
    return 0;
}
