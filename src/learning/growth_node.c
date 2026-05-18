#include "growth_node.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

/* ── 全局 ──────────────────────────────────────────────────── */
static growth_log_t g_log;
static int g_initialized = 0;

static const char *type_names[GROWTH_NUM_TYPES] = {
    "GMM_CONVERGE",
    "FEAR_RESPONSE",
    "CURIOSITY_EXPLORE",
    "CODE_MODIFY",
    "SNAPSHOT_ROLLBACK",
    "TRUST_ESTABLISHED",
    "SELF_CALIBRATE",
    "HUMILITY_FIRST",
    "STRUCT_CHECK",
    "KNOWLEDGE_STOP",
};

/* ── 初始化 ───────────────────────────────────────────────── */
void growth_init(void) {
    memset(&g_log, 0, sizeof(g_log));
    g_initialized = 1;
    printf("  GROWTH: growth node logger initialized (%d types)\n", GROWTH_NUM_TYPES);
}

/* ── 记录生长节点 ─────────────────────────────────────────── */
void growth_record(growth_type_t type, uint64_t tick, float value, const char *name) {
    if (!g_initialized) return;
    if ((int)type < 0 || (int)type >= GROWTH_NUM_TYPES) return;

    /* 同一type只记录第一次 */
    if (g_log.types_seen & (1u << (int)type)) return;

    if (g_log.count >= GROWTH_MAX_NODES) return;

    growth_node_t *n = &g_log.nodes[g_log.count];
    n->type  = type;
    n->tick  = tick;
    n->value = value;
    if (name) {
        strncpy(n->name, name, GROWTH_NAME_LEN - 1);
        n->name[GROWTH_NAME_LEN - 1] = '\0';
    } else {
        strncpy(n->name, type_names[(int)type], GROWTH_NAME_LEN - 1);
        n->name[GROWTH_NAME_LEN - 1] = '\0';
    }

    g_log.types_seen |= (1u << (int)type);
    if (g_log.count == 0) g_log.first_tick = tick;
    g_log.last_tick = tick;
    g_log.count++;

    printf("  GROWTH: #%u %s at tick %" PRIu64 " (value=%.3f)\n",
           g_log.count, n->name, tick, value);
}

/* ── 查询 ─────────────────────────────────────────────────── */
int growth_has(growth_type_t type) {
    if (!g_initialized) return 0;
    if ((int)type < 0 || (int)type >= GROWTH_NUM_TYPES) return 0;
    return (g_log.types_seen & (1u << (int)type)) ? 1 : 0;
}

const growth_node_t *growth_get(growth_type_t type) {
    if (!g_initialized) return NULL;
    if ((int)type < 0 || (int)type >= GROWTH_NUM_TYPES) return NULL;
    if (!(g_log.types_seen & (1u << (int)type))) return NULL;

    for (uint32_t i = 0; i < g_log.count; i++) {
        if (g_log.nodes[i].type == type)
            return &g_log.nodes[i];
    }
    return NULL;
}

int growth_count(void) {
    if (!g_initialized) return 0;
    return (int)g_log.count;
}

/* ── 打印 ─────────────────────────────────────────────────── */
void growth_print(void) {
    if (!g_initialized) return;

    printf("  GROWTH: %u nodes, %u/%u types seen\n",
           g_log.count, __builtin_popcount(g_log.types_seen), GROWTH_NUM_TYPES);
    for (uint32_t i = 0; i < g_log.count; i++) {
        growth_node_t *n = &g_log.nodes[i];
        printf("       [%u] %s tick=%" PRIu64 " value=%.3f\n",
               i, n->name, n->tick, n->value);
    }
}

/* ── 保存/加载 ────────────────────────────────────────────── */
int growth_save(void) {
    if (!g_initialized) return -1;

    FILE *f = fopen("persist/growth_nodes.bin", "wb");
    if (!f) return -1;

    if (fwrite(&g_log, sizeof(g_log), 1, f) != 1) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

int growth_load(void) {
    if (!g_initialized) growth_init();

    FILE *f = fopen("persist/growth_nodes.bin", "rb");
    if (!f) return -1;

    if (fread(&g_log, sizeof(g_log), 1, f) != 1) {
        fclose(f);
        memset(&g_log, 0, sizeof(g_log));
        fprintf(stderr, "  GROWTH: load failed (truncated file)\n");
        return -1;
    }
    fclose(f);

    if (g_log.count > GROWTH_MAX_NODES) g_log.count = 0;
    if (g_log.types_seen >= (1u << GROWTH_NUM_TYPES)) g_log.types_seen = 0;

    printf("  GROWTH: loaded %u growth nodes from disk\n", g_log.count);
    return 0;
}
