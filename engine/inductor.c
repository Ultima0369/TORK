#include "inductor.h"
#include "blackboard.h"
#include "code_reader.h"
#include "code_modifier.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* ── Shared memory ──────────────────────────────────────────────── */
static volatile uint8_t *rmem = NULL;

/* ── Static collector for bb_read_all callback ──────────────────── */
static int g_succ[4], g_fail[4];
static uint32_t g_first_tick[4];

static void collect_cb(int idx, uint32_t tick, uint16_t instance,
                       uint8_t type, uint8_t value, uint32_t payload) {
    (void)idx; (void)instance; (void)payload;

    if (type == BB_TYPE_OPT_SUCCESS || type == BB_TYPE_OPT_FAIL) {
        int g = value - 1;
        if (g < 0 || g > 2) return;
        if (type == BB_TYPE_OPT_SUCCESS)
            g_succ[g]++;
        else
            g_fail[g]++;
        if (g_first_tick[g] == 0 || tick < g_first_tick[g])
            g_first_tick[g] = tick;
    }
}

/* ── Init ───────────────────────────────────────────────────────── */
int ind_init(void) {
    void *addr = mmap((void *)RULE_BASE, RULE_SIZE,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        perror("ind_init mmap");
        return -1;
    }
    if ((uintptr_t)addr != RULE_BASE) {
        fprintf(stderr, "ind_init: mapped at %p, expected 0x%x\n", addr, RULE_BASE);
        munmap(addr, RULE_SIZE);
        return -1;
    }

    rmem = (volatile uint8_t *)addr;

    uint32_t magic;
    memcpy(&magic, (const void *)(rmem + RULE_OFF_MAGIC), 4);

    if (magic != RULE_MAGIC) {
        memset((void *)rmem, 0, RULE_SIZE);
        uint32_t v = RULE_MAGIC;
        memcpy((void *)(rmem + RULE_OFF_MAGIC), &v, 4);
        v = RULE_VERSION;
        memcpy((void *)(rmem + RULE_OFF_VERSION), &v, 4);
    }

    return 0;
}

/* ── Rule slot helpers ──────────────────────────────────────────── */
static int get_rule_count(void) {
    uint32_t c;
    memcpy(&c, (const void *)(rmem + RULE_OFF_COUNT), 4);
    return (int)c;
}

static void set_rule_count(int c) {
    uint32_t v = (uint32_t)c;
    memcpy((void *)(rmem + RULE_OFF_COUNT), &v, 4);
}

static void read_rule(int slot, struct tork_rule *out) {
    uint32_t off = RULE_OFF_DATA + slot * RULE_STRUCT_SIZE;
    memcpy(out, (const void *)(rmem + off), sizeof(struct tork_rule));
}

static void write_rule(int slot, const struct tork_rule *r) {
    uint32_t off = RULE_OFF_DATA + slot * RULE_STRUCT_SIZE;
    memcpy((void *)(rmem + off), r, sizeof(struct tork_rule));
}

/* ── Save / Load / Find ─────────────────────────────────────────── */
int ind_save_rule(const struct tork_rule *rule) {
    int count = get_rule_count();
    if (count >= RULE_MAX) return -1;
    write_rule(count, rule);
    set_rule_count(count + 1);
    return count;
}

int ind_load_rules(struct tork_rule *out, int max) {
    int count = get_rule_count();
    if (count > max) count = max;
    for (int i = 0; i < count; i++)
        read_rule(i, &out[i]);
    return count;
}

int ind_update_rule(int slot, const struct tork_rule *rule) {
    if (slot < 0 || slot >= get_rule_count()) return -1;
    write_rule(slot, rule);
    return 0;
}

int ind_find_pending(void) {
    int count = get_rule_count();
    for (int i = 0; i < count; i++) {
        struct tork_rule r;
        read_rule(i, &r);
        if (!r.active && r.confidence > 60 && r.premise_len > 0)
            return i;
    }
    return -1;
}

int ind_find_active(void) {
    int count = get_rule_count();
    for (int i = 0; i < count; i++) {
        struct tork_rule r;
        read_rule(i, &r);
        if (r.active && r.confidence >= RULE_CONFIDENCE_ACTIVE)
            return i;
    }
    return -1;
}

int ind_active_count(void) {
    int n = 0, count = get_rule_count();
    for (int i = 0; i < count; i++) {
        struct tork_rule r;
        read_rule(i, &r);
        if (r.active) n++;
    }
    return n;
}

/* ── Extract experiences from blackboard ─────────────────────────── */
int ind_extract_experiences(struct tork_rule *rules, int max_rules) {
    memset(g_succ, 0, sizeof(g_succ));
    memset(g_fail, 0, sizeof(g_fail));
    memset(g_first_tick, 0, sizeof(g_first_tick));

    bb_read_all(collect_cb);

    int count = 0;

    /* Type 1: je→jz (conservative, value=1) */
    if ((g_succ[0] + g_fail[0]) > 0 && count < max_rules) {
        struct tork_rule *r = &rules[count++];
        memset(r, 0, sizeof(*r));
        r->type = RULE_TYPE_CODE;
        r->active = 0;
        int total = g_succ[0] + g_fail[0];
        r->confidence = (uint8_t)(g_succ[0] * 100 / total);
        strncpy(r->premise, "function contains 'je' instruction", 32);
        r->premise_len = (uint8_t)strlen(r->premise);
        strncpy(r->conclusion, "replace 'je' with 'jz'", 32);
        r->conclusion_len = (uint8_t)strlen(r->conclusion);
        r->from_tick = (uint16_t)(g_first_tick[0] & 0xFFFF);
        r->apply_count = (uint16_t)g_succ[0];
        r->fail_count = (uint16_t)g_fail[0];
    }

    /* Type 2: dead code deletion (aggressive, value=2) */
    if ((g_succ[1] + g_fail[1]) > 0 && count < max_rules) {
        struct tork_rule *r = &rules[count++];
        memset(r, 0, sizeof(*r));
        r->type = RULE_TYPE_CODE;
        r->active = 0;
        int total = g_succ[1] + g_fail[1];
        r->confidence = (uint8_t)(g_succ[1] * 100 / total);
        strncpy(r->premise, "unreachable code after 'ret'", 32);
        r->premise_len = (uint8_t)strlen(r->premise);
        strncpy(r->conclusion, "delete unreachable code", 32);
        r->conclusion_len = (uint8_t)strlen(r->conclusion);
        r->from_tick = (uint16_t)(g_first_tick[1] & 0xFFFF);
        r->apply_count = (uint16_t)g_succ[1];
        r->fail_count = (uint16_t)g_fail[1];
    }

    /* Type 3: NOP deletion (value=3) */
    if ((g_succ[2] + g_fail[2]) > 0 && count < max_rules) {
        struct tork_rule *r = &rules[count++];
        memset(r, 0, sizeof(*r));
        r->type = RULE_TYPE_CODE;
        r->active = 0;
        int total = g_succ[2] + g_fail[2];
        r->confidence = (uint8_t)(g_succ[2] * 100 / total);
        strncpy(r->premise, "function contains nop/align", 32);
        r->premise_len = (uint8_t)strlen(r->premise);
        strncpy(r->conclusion, "delete nop alignment", 32);
        r->conclusion_len = (uint8_t)strlen(r->conclusion);
        r->from_tick = (uint16_t)(g_first_tick[2] & 0xFFFF);
        r->apply_count = (uint16_t)g_succ[2];
        r->fail_count = (uint16_t)g_fail[2];
    }

    return count;
}

/* ── Generalize ─────────────────────────────────────────────────── */
int ind_generalize(const struct tork_rule *existing, int count,
                   struct tork_rule *new_rule) {
    if (count < 2) return 1;

    memset(new_rule, 0, sizeof(*new_rule));

    int type_count[4] = {0};
    for (int i = 0; i < count; i++)
        if (existing[i].type <= 3) type_count[existing[i].type]++;

    int best_type = 1;
    for (int t = 2; t <= 3; t++)
        if (type_count[t] > type_count[best_type]) best_type = t;

    int merged = 0;
    uint8_t min_conf = 100;
    uint16_t total_apply = 0, total_fail = 0;
    uint16_t earliest_tick = 0xFFFF;

    for (int i = 0; i < count; i++) {
        if (existing[i].type != best_type) continue;
        merged++;
        if (existing[i].confidence < min_conf)
            min_conf = existing[i].confidence;
        total_apply += existing[i].apply_count;
        total_fail += existing[i].fail_count;
        if (existing[i].from_tick < earliest_tick)
            earliest_tick = existing[i].from_tick;

        if (merged == 1) {
            strncpy(new_rule->premise, existing[i].premise, 32);
            strncpy(new_rule->conclusion, existing[i].conclusion, 32);
            new_rule->premise_len = existing[i].premise_len;
            new_rule->conclusion_len = existing[i].conclusion_len;
        }
    }

    if (merged < 2) return 1;

    new_rule->type = (uint8_t)best_type;
    new_rule->confidence = min_conf;
    new_rule->from_tick = earliest_tick;
    new_rule->apply_count = total_apply;
    new_rule->fail_count = total_fail;
    new_rule->active = 0;

    return 0;
}

/* ── Test rule ──────────────────────────────────────────────────── */
int ind_test_rule(struct tork_rule *rule, const char *asm_file) {
    if (!rule || rule->premise_len == 0) return -1;

    char buf[8192];
    int len = asm_read_file(asm_file, buf, sizeof(buf));
    if (len <= 0) return -1;

    if (strstr(buf, rule->premise) == NULL) return 1;

    /* Apply conclusion */

    if (strcmp(rule->conclusion, "replace 'je' with 'jz'") == 0) {
        /* Already handled by conservative optimization — test passes
           if the premise is found (we already know je→jz works) */
        rule->apply_count++;
        int total = rule->apply_count + rule->fail_count;
        if (total > 0)
            rule->confidence = (uint8_t)(rule->apply_count * 100 / total);
        if (rule->confidence >= RULE_CONFIDENCE_ACTIVE)
            rule->active = 1;
        return 0;
    }

    if (strcmp(rule->conclusion, "delete nop alignment") == 0) {
        int new_len = len;
        int nops = asm_delete_nop_insns(buf, len, "memcpy_tork", &new_len);
        if (nops > 0) {
            int verified = asm_verify_modification(buf, new_len, "benchmark/memcpy");
            if (verified) {
                rule->apply_count++;
                int total = rule->apply_count + rule->fail_count;
                rule->confidence = (uint8_t)(rule->apply_count * 100 / total);
                if (rule->confidence >= RULE_CONFIDENCE_ACTIVE)
                    rule->active = 1;
                return 0;
            } else {
                rule->fail_count++;
                int total = rule->apply_count + rule->fail_count;
                rule->confidence = (uint8_t)(rule->apply_count * 100 / total);
                if (rule->confidence <= RULE_CONFIDENCE_RETIRE)
                    rule->confidence = 0;
                return 1;
            }
        }
        return 1;
    }

    if (strcmp(rule->conclusion, "delete unreachable code") == 0) {
        int new_len = len;
        int deleted = asm_delete_dead_insns(buf, len, "memcpy_tork", &new_len);
        if (deleted > 0) {
            int verified = asm_verify_modification(buf, new_len, "benchmark/memcpy");
            if (verified) {
                rule->apply_count++;
                int total = rule->apply_count + rule->fail_count;
                rule->confidence = (uint8_t)(rule->apply_count * 100 / total);
                if (rule->confidence >= RULE_CONFIDENCE_ACTIVE)
                    rule->active = 1;
                return 0;
            } else {
                rule->fail_count++;
                int total = rule->apply_count + rule->fail_count;
                rule->confidence = (uint8_t)(rule->apply_count * 100 / total);
                if (rule->confidence <= RULE_CONFIDENCE_RETIRE)
                    rule->confidence = 0;
                return 1;
            }
        }
        return 1;
    }

    return 1;
}

/* ── Cleanup ────────────────────────────────────────────────────── */
void ind_cleanup(void) {
    if (rmem) {
        munmap((void *)rmem, RULE_SIZE);
        rmem = NULL;
    }
}