#ifndef TORK_GRID_H
#define TORK_GRID_H

#include <stdint.h>

/* ── TORK Grid Emergence — Soul Visualization ───────────────
 *  80×40 grid that displays TORK's real-time internal state.
 *  Each pixel is a lightweight TORK instance, but the overall
 *  pattern is driven by actual soul data from the running engine.
 *
 *  Zones:
 *    Row  0:    Status bar (tick/drive/stress/gen)
 *    Rows 1-8:  Heartbeat waveform (scrolling)
 *    Rows 10-15: Instinct bars (fear/desire/curiosity)
 *    Rows 17-22: Branch health indicator
 *    Rows 24-33: Experience heatmap
 *    Rows 35-39: Bottom status
 * ──────────────────────────────────────────────────────────── */

#define GRID_W    80
#define GRID_H    40
#define GRID_N    (GRID_W * GRID_H)

/* ── Soul feed data (pushed by engine each tick) ────────── */
typedef struct {
    uint32_t tick;              /* Heartbeat counter */
    int8_t   drive;             /* Overall drive (-128..127) */
    uint8_t  hw_stress;         /* CPU stress (0-3) */
    uint32_t gen_count;         /* Evolution generation */
    int      active_branches;   /* Number of active branch contexts */
    uint32_t experience_count;  /* Total experiences accumulated */
    int      peer_count;        /* Distributed peers */
    uint8_t  energy_mode;       /* Current energy mode */
    
    /* Instinct values (0.0-1.0 scaled to 0-100) */
    uint8_t  fear;              /* Fear level */
    uint8_t  desire;            /* Desire level */
    uint8_t  curiosity;         /* Curiosity level */
    
    /* Branch health (up to 8 branches) */
    int8_t   branch_drive[8];   /* Drive of each branch */
    uint32_t branch_ticks[8];   /* Ticks lived by each branch */
    
    /* Recent experience outcomes (up to 16, scaled -100..100 → 0..200) */
    int8_t   recent_outcomes[16];
    uint8_t  outcome_count;
} grid_soul_feed_t;

/* ── Pixel state ──────────────────────────────────────────── */
typedef struct {
    uint8_t  r, g, b;           /* RGB color 0-255 */
    uint8_t  brightness;         /* 0-255 */
    int8_t   curiosity;          /* -128..127 */
    uint8_t  tick;               /* Local tick */
    uint8_t  neighbor_sum;       /* Sum of neighbor brightnesses */
    uint8_t  decay_rate;         /* How fast brightness fades */
    uint8_t  influence;          /* How much influence on neighbors */
} tork_pixel_t;

/* ── Grid ─────────────────────────────────────────────────── */
typedef struct {
    tork_pixel_t pixels[GRID_N];
    uint64_t     global_tick;
    uint8_t      paused;
    uint8_t      display_mode;   /* 0=emergent, 1=soul-viz */
    grid_soul_feed_t soul;       /* Latest soul data from engine */
    
    /* Waveform history (for heartbeat scrolling) */
    uint8_t      waveform[GRID_W];
    uint8_t      waveform_pos;
} tork_grid_t;

/* ── Public API ───────────────────────────────────────────── */

void grid_init(tork_grid_t *g);

/* Feed real-time soul data into the grid (called by engine) */
void grid_feed_soul(tork_grid_t *g, const grid_soul_feed_t *soul);

/* Run one tick of all pixels */
void grid_tick(tork_grid_t *g);

tork_pixel_t *grid_at(tork_grid_t *g, int x, int y);
uint8_t grid_neighbor_sum(tork_grid_t *g, int x, int y);

/* Render grid to terminal (ANSI escape codes) */
void grid_render(tork_grid_t *g);

/* Render a single frame with cursor reset */
void grid_frame(tork_grid_t *g);

#endif /* TORK_GRID_H */
