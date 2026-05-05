#include "blackboard.h"
#include "soul_access.h"
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static volatile uint8_t *bb = NULL;

/* Helper: get current tick from our own soul (we'll pass it in via bb_write) */
static uint32_t bb_current_tick;

void bb_set_tick(uint32_t tick) {
    bb_current_tick = tick;
}

int bb_init(void) {
    void *addr = mmap((void *)BB_BASE, BB_SIZE,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        perror("bb_init mmap");
        return -1;
    }
    if ((uintptr_t)addr != BB_BASE) {
        fprintf(stderr, "bb_init: mapped at %p, expected 0x%x\n", addr, BB_BASE);
        munmap(addr, BB_SIZE);
        return -1;
    }

    bb = (volatile uint8_t *)addr;

    uint32_t magic;
    memcpy(&magic, (const void *)(bb + BB_OFF_MAGIC), 4);

    if (magic != BB_MAGIC) {
        /* Fresh blackboard — initialize */
        memset((void *)bb, 0, BB_SIZE);
        uint32_t v = BB_MAGIC;
        memcpy((void *)(bb + BB_OFF_MAGIC), &v, 4);
        v = BB_VERSION;
        memcpy((void *)(bb + BB_OFF_VERSION), &v, 4);
        uint32_t cursor = BB_ENTRY_START;
        memcpy((void *)(bb + BB_OFF_WRITE_CURSOR), &cursor, 4);
    }

    return 0;
}

int bb_write(uint8_t type, uint8_t value, uint32_t payload) {
    if (!bb) return -1;

    uint32_t cursor;
    memcpy(&cursor, (const void *)(bb + BB_OFF_WRITE_CURSOR), 4);

    if (cursor < BB_ENTRY_START || cursor >= BB_ENTRY_END)
        cursor = BB_ENTRY_START;

    /* Build entry locally, then copy atomically-ish */
    uint8_t entry[BB_ENTRY_SIZE];
    memset(entry, 0, BB_ENTRY_SIZE);

    uint32_t tick = bb_current_tick;
    memcpy(entry + 0, &tick, 4);

    uint16_t inst_id = (uint16_t)getpid();
    memcpy(entry + 4, &inst_id, 2);

    entry[6] = type;
    entry[7] = value;
    memcpy(entry + 8, &payload, 4);

    memcpy((void *)(bb + cursor), entry, BB_ENTRY_SIZE);

    /* Advance cursor */
    cursor += BB_ENTRY_SIZE;
    if (cursor >= BB_ENTRY_END)
        cursor = BB_ENTRY_START;
    memcpy((void *)(bb + BB_OFF_WRITE_CURSOR), &cursor, 4);

    /* Increment entry count (saturate at UINT32_MAX to prevent wrap) */
    uint32_t count;
    memcpy(&count, (const void *)(bb + BB_OFF_ENTRY_COUNT), 4);
    if (count < UINT32_MAX) count++;
    memcpy((void *)(bb + BB_OFF_ENTRY_COUNT), &count, 4);

    int slot = (int)((cursor - BB_ENTRY_SIZE - BB_ENTRY_START) / BB_ENTRY_SIZE);
    return slot;
}

int bb_read_all(bb_callback_t callback) {
    if (!bb) return -1;

    uint32_t count;
    memcpy(&count, (const void *)(bb + BB_OFF_ENTRY_COUNT), 4);

    int valid = 0;
    for (int i = 0; i < BB_MAX_ENTRIES; i++) {
        uint32_t off = BB_ENTRY_START + i * BB_ENTRY_SIZE;
        uint32_t tick;
        memcpy(&tick, (const void *)(bb + off), 4);

        /* Skip uninitialized or corrupt entries */
        if (tick == 0 || tick > 0xFFFFFF00) continue;

        uint16_t instance;
        memcpy(&instance, (const void *)(bb + off + 4), 2);
        uint8_t type = bb[off + 6];
        uint8_t value = bb[off + 7];
        uint32_t payload;
        memcpy(&payload, (const void *)(bb + off + 8), 4);

        callback(i, tick, instance, type, value, payload);
        valid++;
    }
    return valid;
}

uint32_t bb_global_optimizations(void) {
    if (!bb) return 0;
    uint32_t v;
    memcpy(&v, (const void *)(bb + BB_OFF_TOTAL_OPT), 4);
    return v;
}

uint32_t bb_global_fissions(void) {
    if (!bb) return 0;
    uint32_t v;
    memcpy(&v, (const void *)(bb + BB_OFF_TOTAL_FISS), 4);
    return v;
}

uint32_t bb_global_errors(void) {
    if (!bb) return 0;
    uint32_t v;
    memcpy(&v, (const void *)(bb + BB_OFF_TOTAL_ERR), 4);
    return v;
}

void bb_inc_optimizations(void) {
    if (!bb) return;
    uint32_t v;
    memcpy(&v, (const void *)(bb + BB_OFF_TOTAL_OPT), 4);
    v++;
    memcpy((void *)(bb + BB_OFF_TOTAL_OPT), &v, 4);
}

void bb_inc_fissions(void) {
    if (!bb) return;
    uint32_t v;
    memcpy(&v, (const void *)(bb + BB_OFF_TOTAL_FISS), 4);
    v++;
    memcpy((void *)(bb + BB_OFF_TOTAL_FISS), &v, 4);
}

void bb_inc_errors(void) {
    if (!bb) return;
    uint32_t v;
    memcpy(&v, (const void *)(bb + BB_OFF_TOTAL_ERR), 4);
    v++;
    memcpy((void *)(bb + BB_OFF_TOTAL_ERR), &v, 4);
}

void bb_cleanup(void) {
    if (bb) {
        munmap((void *)bb, BB_SIZE);
        bb = NULL;
    }
}
