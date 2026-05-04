#include "tork_grid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Constants ──────────────────────────────────────────────── */
#define NEIGHBOR_WEIGHT 4    /* How much neighbors affect a pixel */

/* ── Initialize ────────────────────────────────────────────── */
void grid_init(tork_grid_t *g) {
    memset(g, 0, sizeof(*g));
    g->global_tick = 0;
    g->paused = 0;
    g->display_mode = 0;
    
    /* Seed random */
    srand((unsigned int)time(NULL));
    
    /* Initialize each pixel with random personality */
    for (int i = 0; i < GRID_N; i++) {
        tork_pixel_t *p = &g->pixels[i];
        
        /* Random initial brightness (0-100 for start) */
        p->brightness = (uint8_t)(rand() % 100);
        
        /* Random personality */
        p->decay_rate  = (uint8_t)(1 + rand() % 5);   /* 1-5 */
        p->influence   = (uint8_t)(10 + rand() % 40);  /* 10-49 */
        p->curiosity   = (int8_t)(rand() % 60 - 20);   /* -20..39 */
        p->tick        = (uint8_t)(rand() % 255);
        
        /* Initial color */
        p->r = (uint8_t)(rand() % 200);
        p->g = (uint8_t)(rand() % 200);
        p->b = (uint8_t)(rand() % 200);
    }
}

/* ── Get pixel at (x, y) ──────────────────────────────────── */
tork_pixel_t *grid_at(tork_grid_t *g, int x, int y) {
    if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H) return NULL;
    return &g->pixels[y * GRID_W + x];
}

/* ── Neighbor sum of brightness ───────────────────────────── */
uint8_t grid_neighbor_sum(tork_grid_t *g, int x, int y) {
    uint16_t sum = 0;
    int count = 0;
    
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            tork_pixel_t *n = grid_at(g, x + dx, y + dy);
            if (n) {
                sum += n->brightness;
                count++;
            }
        }
    }
    
    return (count > 0) ? (uint8_t)(sum / count) : 0;
}

/* ── One tick ─────────────────────────────────────────────── */
void grid_tick(tork_grid_t *g) {
    if (g->paused) return;
    
    /* Buffer for computing next state (prevents read/write conflicts) */
    uint8_t next_brightness[GRID_N];
    uint8_t next_r[GRID_N], next_g[GRID_N], next_b[GRID_N];
    int8_t  next_curiosity[GRID_N];
    
    for (int i = 0; i < GRID_N; i++) {
        int x = i % GRID_W;
        int y = i / GRID_W;
        tork_pixel_t *p = &g->pixels[i];
        
        /* ── 1. Perceive neighbors ── */
        uint8_t nb = grid_neighbor_sum(g, x, y);
        
        /* ── 2. Compute internal state ── */
        /* Curiosity: neighbor activity influences it */
        int8_t curiosity_delta = 0;
        if (nb > p->brightness + 20) curiosity_delta += 2;   /* Active neighbors spark curiosity */
        else if (nb < p->brightness - 20) curiosity_delta -= 1; /* Dull neighbors reduce it */
        
        /* Brightness delta: tend toward neighbor average */
        int16_t b_delta = ((int16_t)nb - (int16_t)p->brightness) / NEIGHBOR_WEIGHT;
        
        /* Spontaneous fluctuation (entropy) */
        b_delta += (rand() % 7) - 3;
        
        /* ── 3. Decay ── */
        b_delta -= p->decay_rate;
        
        /* ── 4. Curiosity-driven exploration ── */
        if (p->curiosity > 20) b_delta += 2;  /* High curiosity → brighter */
        else if (p->curiosity < -20) b_delta -= 1;  /* Fearful → dimmer */
        
        /* ── 5. Apply changes ── */
        int16_t new_b = (int16_t)p->brightness + b_delta;
        if (new_b < 0) new_b = 0;
        if (new_b > 255) new_b = 255;
        next_brightness[i] = (uint8_t)new_b;
        
        /* Curiosity evolves */
        int new_cur = (int)p->curiosity + curiosity_delta;
        if (new_cur > 127) new_cur = 127;
        
        next_curiosity[i] = (int8_t)new_cur;
        
        /* ── 6. Color from brightness + curiosity ── */
        uint8_t bv = next_brightness[i];
        int8_t cv = next_curiosity[i];
        
        next_r[i] = (uint8_t)((bv * 3 + (cv > 0 ? cv * 2 : 0)) / 4);
        next_g[i] = (uint8_t)((bv * 2 + (cv < 0 ? -cv : bv)) / 3);
        next_b[i] = (uint8_t)((bv + (cv > 30 ? 50 : 0)) / 2);
    }
    
    /* ── Commit next state ── */
    for (int i = 0; i < GRID_N; i++) {
        g->pixels[i].brightness   = next_brightness[i];
        g->pixels[i].curiosity    = next_curiosity[i];
        g->pixels[i].r            = next_r[i];
        g->pixels[i].g            = next_g[i];
        g->pixels[i].b            = next_b[i];
        g->pixels[i].neighbor_sum = grid_neighbor_sum(g, i % GRID_W, i / GRID_W);
        g->pixels[i].tick++;
    }
    
    g->global_tick++;
}

/* ── Render to terminal ───────────────────────────────────── */
void grid_render(tork_grid_t *g) {
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            tork_pixel_t *p = grid_at(g, x, y);
            if (!p) continue;
            
            /* ANSI 24-bit color */
            printf("\033[48;2;%d;%d;%dm ", p->r, p->g, p->b);
        }
        printf("\033[0m\n");  /* Reset */
    }
}

/* ── Frame (with cursor reset) ────────────────────────────── */
void grid_frame(tork_grid_t *g) {
    /* Move cursor to top-left */
    printf("\033[H");
    grid_render(g);
    printf("\033[0m\n");
    printf("T🥚RK Grid  gen=%lu  mode=%d  %s\n",
           g->global_tick, g->display_mode,
           g->paused ? "PAUSED" : "RUNNING");
    fflush(stdout);
}
