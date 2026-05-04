#ifndef TORK_GRID_H
#define TORK_GRID_H

#include <stdint.h>

/* ── TORK Grid Emergence ────────────────────────────────────
 *  80×40 grid of lightweight TORK instances.
 *  Each pixel has a simplified Soul and interacts with 8 neighbors.
 *  Display via ANSI terminal escape codes.
 * ──────────────────────────────────────────────────────────── */

#define GRID_W    80
#define GRID_H    40
#define GRID_N    (GRID_W * GRID_H)

/* ── Pixel state ──────────────────────────────────────────── */
typedef struct {
    uint8_t  r, g, b;          /* RGB color 0-255 */
    uint8_t  brightness;        /* 0-255 */
    
    /* Internal state */
    int8_t   curiosity;         /* -128..127 */
    uint8_t  tick;              /* Local tick */
    uint8_t  neighbor_sum;      /* Sum of neighbor brightnesses */
    
    /* Personality (stable over time) */
    uint8_t  decay_rate;        /* How fast brightness fades */
    uint8_t  influence;         /* How much influence on neighbors */
} tork_pixel_t;

/* ── Grid ─────────────────────────────────────────────────── */
typedef struct {
    tork_pixel_t pixels[GRID_N];
    uint64_t     global_tick;
    uint8_t      paused;
    uint8_t      display_mode;  /* 0=heat, 1=curiosity, 2=neighbor */
} tork_grid_t;

/* ── Public API ───────────────────────────────────────────── */

/* Initialize grid with random states */
void grid_init(tork_grid_t *g);

/* Run one tick of all pixels */
void grid_tick(tork_grid_t *g);

/* Get pixel at (x, y) — returns NULL if out of bounds */
tork_pixel_t *grid_at(tork_grid_t *g, int x, int y);

/* Get neighbor sum of brightness for a pixel */
uint8_t grid_neighbor_sum(tork_grid_t *g, int x, int y);

/* Render grid to terminal (ANSI escape codes) */
void grid_render(tork_grid_t *g);

/* Render a single frame with cursor reset */
void grid_frame(tork_grid_t *g);

#endif /* TORK_GRID_H */
