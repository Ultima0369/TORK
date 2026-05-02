#include "soul_access.h"
#include "instinct.h"
#include "code_reader.h"
#include "code_modifier.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static pid_t core_pid = 0;

static void cleanup_core(int sig) {
    if (core_pid > 0) {
        kill(core_pid, SIGTERM);
        waitpid(core_pid, NULL, 0);
    }
    _exit(0);
}

static int start_core(void) {
    core_pid = fork();
    if (core_pid == 0) {
        FILE *dn = fopen("/dev/null", "w");
        if (dn) { dup2(fileno(dn), STDOUT_FILENO); fclose(dn); }
        execl("./tork_core", "tork_core", NULL);
        _exit(1);
    }
    if (core_pid < 0) return -1;
    usleep(200000);
    return 0;
}

static int parse_proc_status_int(pid_t pid, const char *field, uint32_t *out) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    size_t flen = strlen(field);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, field, flen) == 0) {
            *out = (uint32_t)strtoul(line + flen, NULL, 10);
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return -1;
}

int main(int argc, char **argv) {
    int rounds = 100;
    if (argc > 1) rounds = atoi(argv[1]);
    if (rounds < 1) rounds = 100;

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

    /* write self_pid once */
    {
        uint32_t pid_val = (uint32_t)core_pid;
        soul_write_buf(&soul, S_SELF_PID, &pid_val, 4);
    }

    /* write ppid once */
    {
        uint32_t val;
        if (parse_proc_status_int(core_pid, "PPid:\t", &val) == 0) {
            uint16_t v = (val > 65535) ? 65535 : (uint16_t)val;
            soul_write_buf(&soul, S_PPID, &v, 2);
        }
    }

    printf("TORK engine started. core PID=%d\n", core_pid);
    printf("polling 500ms | instinct 10 | env 50 | code 200 | modify 300\n\n");

    /* code modification tracking */
    int mod_attempted = 0;

    for (int i = 0; i < rounds; i++) {
        int rc = soul_read(&soul);
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
        };

        tork_instinct_t inst = instinct_evaluate(&inp);

        int drive = (int)((inst.desire - inst.fear + inst.curiosity) * 100.0f);
        if (drive > 127) drive = 127;
        if (drive < -128) drive = -128;
        soul_set_drive(&soul, (int8_t)drive);

        /* every 200 rounds: code reading */
        if (i % 200 == 0) {
            char asm_buf[8192];
            int alen = asm_read_file("benchmark/memcpy/ref.s", asm_buf, sizeof(asm_buf));
            if (alen > 0) {
                int insns = asm_count_insns_in_func(asm_buf, alen, "memcpy_tork");

                char opcodes[32][8];
                int opc_count = asm_extract_opcodes(asm_buf, alen, "memcpy_tork", opcodes, 32);

                int cm = 0, ca = 0, cc = 0, co = 0;
                asm_classify_insns(asm_buf, alen, "memcpy_tork", &cm, &ca, &cc, &co);

                printf("[%4d] tick=%-6u reading memcpy_tork: %d insns\n", i, inp.tick, insns);
                printf("       opcodes:");
                int show = opc_count > 10 ? 10 : opc_count;
                for (int k = 0; k < show; k++) printf(" %s", opcodes[k]);
                printf("\n");
                printf("       class: mov=%d arith=%d control=%d other=%d\n", cm, ca, cc, co);

                /* write code stats to soul */
                {
                    uint8_t stats[10];
                    memset(stats, 0, sizeof(stats));
                    *(uint16_t*)(stats + 0) = (uint16_t)insns;
                    *(uint16_t*)(stats + 2) = (uint16_t)cm;
                    *(uint16_t*)(stats + 4) = (uint16_t)ca;
                    *(uint16_t*)(stats + 6) = (uint16_t)cc;
                    *(uint16_t*)(stats + 8) = (uint16_t)co;
                    soul_write_buf(&soul, S_CODE_INSNS, stats, 10);
                }

                inp.code_insns = (uint16_t)insns;
                inp.code_ctrl  = (uint16_t)cc;
                inst = instinct_evaluate(&inp);
                drive = (int)((inst.desire - inst.fear + inst.curiosity) * 100.0f);
                if (drive > 127) drive = 127;
                if (drive < -128) drive = -128;
                soul_set_drive(&soul, (int8_t)drive);
            }
        }

        /* every 300 rounds: code modification attempt */
        if (i % 300 == 0 && !mod_attempted) {
            char asm_buf[8192];
            char backup[8192];
            int alen = asm_read_file("benchmark/memcpy/ref.s", asm_buf, sizeof(asm_buf));
            if (alen > 0) {
                int backup_len = alen;
                memcpy(backup, asm_buf, alen);

                /* try replacing je with jz (exact same opcode, different mnemonic) */
                int rep = asm_replace_operand(asm_buf, alen, "memcpy_tork",
                                              "\tje\t", "\tjz\t", 1);
                if (rep == 1) {
                    /* same length, no len adjustment needed */

                    int verified = asm_verify_modification(asm_buf, alen, "benchmark/memcpy");
                    if (verified) {
                        FILE *f = fopen("benchmark/memcpy/ref.s", "w");
                        if (f) {
                            fwrite(asm_buf, 1, alen, f);
                            fclose(f);
                        }
                        printf("[%4d] tick=%-6u MODIFY SUCCESS: replaced je with jz\n",
                               i, inp.tick);
                        uint8_t ms = 1;
                        soul_write_buf(&soul, S_CODE_MOD_SUCCESS, &ms, 1);
                        inp.code_mod_success = 1;
                    } else {
                        asm_rollback(asm_buf, sizeof(asm_buf), backup, backup_len);
                        printf("[%4d] tick=%-6u MODIFY FAILED: je→jz rejected by assembler\n",
                               i, inp.tick);
                        uint8_t ms = 2;
                        soul_write_buf(&soul, S_CODE_MOD_SUCCESS, &ms, 1);
                        inp.code_mod_success = 2;
                    }
                } else {
                    printf("[%4d] tick=%-6u MODIFY SKIP: je not found in memcpy_tork\n",
                           i, inp.tick);
                }

                inst = instinct_evaluate(&inp);
                drive = (int)((inst.desire - inst.fear + inst.curiosity) * 100.0f);
                if (drive > 127) drive = 127;
                if (drive < -128) drive = -128;
                soul_set_drive(&soul, (int8_t)drive);
                mod_attempted = 1;
            }
        }

        /* every 10 rounds: print state */
        if (i % 10 == 0) {
            printf("[%4d] tick=%-6u pid=%-5u ppid=%-4u drive=%+4d fear=%.1f desire=%.1f curiosity=%.1f\n",
                   i, inp.tick, soul_self_pid(&soul), soul_ppid(&soul),
                   drive, inst.fear, inst.desire, inst.curiosity);
        }

        usleep(500000);
    }

    printf("\nshutting down core (pid %d)...\n", core_pid);
    kill(core_pid, SIGTERM);
    int st;
    waitpid(core_pid, &st, 0);
    printf("core exited.\n");

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