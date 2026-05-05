/* ── TORK Grid ↔ Engine Soul Connector ──────────────────────
 *  Bridge between the running engine and the grid visualizer.
 *  The engine writes soul data to /dev/shm/tork_soul.bin,
 *  the grid reader picks it up and renders it.
 *
 *  Engine side:  write_soul_to_grid()
 *  Grid side:    read_soul_from_grid()
 * ──────────────────────────────────────────────────────────── */

#include "tork_grid.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define SOUL_GRID_SHM "/dev/shm/tork_soul.bin"
#define SOUL_GRID_SIZE (sizeof(grid_soul_feed_t) + 8)

/* ── Header at top of shared memory ──────────────────────── */
typedef struct {
    uint64_t write_count;   /* Incremented each write (for sync) */
    grid_soul_feed_t soul;
} __attribute__((packed)) soul_grid_shm_t;

/* ── Engine side: write soul data to shared memory ──────────── */
static int g_shm_fd = -1;
static soul_grid_shm_t *g_shm = NULL;

int grid_engine_init(void) {
    /* Try file fallback first (more portable) */
    g_shm_fd = open("/tmp/tork_soul_grid.bin", O_CREAT | O_RDWR, 0666);
    if (g_shm_fd < 0) {
        /* Try shared memory */
        g_shm_fd = shm_open(SOUL_GRID_SHM, O_CREAT | O_RDWR, 0666);
        if (g_shm_fd < 0) return -1;
    }
    
    ftruncate(g_shm_fd, SOUL_GRID_SIZE);
    
    g_shm = (soul_grid_shm_t*)mmap(NULL, SOUL_GRID_SIZE,
                                    PROT_READ | PROT_WRITE,
                                    MAP_SHARED, g_shm_fd, 0);
    if (g_shm == MAP_FAILED) {
        close(g_shm_fd);
        g_shm_fd = -1;
        return -1;
    }
    
    /* Initialize */
    memset(g_shm, 0, SOUL_GRID_SIZE);
    
    return 0;
}

void grid_engine_write(const grid_soul_feed_t *soul) {
    if (!g_shm || !soul) return;
    g_shm->write_count++;
    memcpy(&g_shm->soul, soul, sizeof(grid_soul_feed_t));
}

void grid_engine_cleanup(void) {
    if (g_shm) {
        munmap(g_shm, SOUL_GRID_SIZE);
        g_shm = NULL;
    }
    if (g_shm_fd >= 0) {
        close(g_shm_fd);
        g_shm_fd = -1;
    }
    shm_unlink(SOUL_GRID_SHM);
    remove("/tmp/tork_soul_grid.bin");
}

/* ── Grid side: read soul data from shared memory ──────────── */
int grid_viewer_init(void) {
    /* Try file first */
    g_shm_fd = open("/tmp/tork_soul_grid.bin", O_RDONLY);
    if (g_shm_fd < 0) {
        /* Try shared memory */
        g_shm_fd = shm_open(SOUL_GRID_SHM, O_RDONLY, 0);
        if (g_shm_fd < 0) return -1;
    }
    
    g_shm = (soul_grid_shm_t*)mmap(NULL, SOUL_GRID_SIZE,
                                    PROT_READ, MAP_SHARED, g_shm_fd, 0);
    if (g_shm == MAP_FAILED) {
        close(g_shm_fd);
        g_shm_fd = -1;
        return -1;
    }
    
    return 0;
}

int grid_viewer_read(grid_soul_feed_t *out) {
    if (!g_shm || !out) return -1;
    memcpy(out, &g_shm->soul, sizeof(grid_soul_feed_t));
    return 0;
}

void grid_viewer_cleanup(void) {
    if (g_shm) {
        munmap(g_shm, SOUL_GRID_SIZE);
        g_shm = NULL;
    }
    if (g_shm_fd >= 0) {
        close(g_shm_fd);
        g_shm_fd = -1;
    }
}
