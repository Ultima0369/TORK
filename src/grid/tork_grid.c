#include "tork_grid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Constants ──────────────────────────────────────────────── */
#define NEIGHBOR_WEIGHT 4
#define WAVE_HISTORY    GRID_W  /* 80 columns of waveform history */

/* ── Helpers ───────────────────────────────────────────────── */
static uint8_t clamp16(int16_t v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static uint8_t scale(int val, int min_in, int max_in, int min_out, int max_out) {
    if (val < min_in) val = min_in;
    if (val > max_in) val = max_in;
    float ratio = (float)(val - min_in) / (float)(max_in - min_in);
    return (uint8_t)(min_out + ratio * (max_out - min_out));
}

/* ── Zone renderers (called by grid_render) ─────────────────── */

/* Row 0: Status bar */
static void render_status_bar(tork_grid_t *g, char *line, int max_len) {
    const grid_soul_feed_t *s = &g->soul;
    snprintf(line, max_len,
        "♥%05u D%+03d S%d G%03u | B%d E%04u P%d M%d",
        s->tick, s->drive, s->hw_stress, s->gen_count,
        s->active_branches, s->experience_count, s->peer_count, s->energy_mode);
}

/* Rows 1-8: Heartbeat waveform (8 rows high, scrolls right) */
static void render_waveform(tork_grid_t *g, int y_global, char *line, int max_len) {
    (void)max_len;
    const grid_soul_feed_t *s = &g->soul;
    int wave_row = y_global - 1;  /* 0..7 within waveform */
    int wave_height = 8;
    
    for (int x = 0; x < GRID_W && x < max_len - 1; x++) {
        uint8_t wv = g->waveform[x];  /* 0..255 */
        /* Map to 8 rows: high values appear at top */
        int threshold = 255 - (wave_row * 255 / wave_height);
        int next_thresh = 255 - ((wave_row + 1) * 255 / wave_height);
        
        tork_pixel_t *p = grid_at(g, x, y_global);
        if (!p) { line[x] = ' '; continue; }
        
        if (wv >= threshold) {
            /* Active pixel */
            uint8_t intensity = scale(wv, next_thresh, 256, 80, 255);
            p->r = intensity;
            p->g = (uint8_t)(intensity / 3);
            p->b = (uint8_t)(intensity / 2);
            p->brightness = intensity;
            line[x] = ' ';  /* Background color will show */
        } else if (wv >= next_thresh) {
            /* Semi-active (gradient) */
            uint8_t dim = scale(wv, next_thresh / 2, next_thresh, 20, 80);
            p->r = dim;
            p->g = dim / 3;
            p->b = dim / 2;
            p->brightness = dim;
            line[x] = ' ';
        } else {
            /* Inactive */
            p->r = 5 + (wave_row * 3);
            p->g = 2 + (wave_row * 2);
            p->b = 10;
            p->brightness = 10 + (wave_row * 3);
            line[x] = ' ';
        }
        
        /* Drive color overlay for the top of the wave */
        if (wave_row == 0 && s->drive > 30) {
            p->r = (uint8_t)(p->r + scale(s->drive, 30, 127, 0, 60));
            p->g = (uint8_t)(p->g + 20);
        }
    }
    line[GRID_W] = '\0';
}

/* Rows 10-15: Instinct bars (fear/desire/curiosity) */
static void render_instinct_bars(tork_grid_t *g, int y_global, char *line, int max_len) {
    (void)max_len;
    const grid_soul_feed_t *s = &g->soul;
    
    int bar_row = y_global - 10;  /* 0..5 */
    float bar_values[3] = {
        s->fear / 100.0f,
        s->desire / 100.0f,
        s->curiosity / 100.0f
    };
    const char *labels[3] = {"FR", "DE", "CU"};
    uint8_t colors[3][3] = {{200,0,0}, {0,150,0}, {0,100,220}};
    
    memset(line, ' ', GRID_W);
    line[GRID_W] = '\0';
    
    for (int b = 0; b < 3; b++) {
        int label_x = b * 27 + 2;
        int bar_x = label_x + 4;
        int bar_len = 20;
        
        /* Label row */
        int filled = (int)(bar_values[b] * bar_len);
        if (filled < 0) filled = 0;
        if (filled > bar_len) filled = bar_len;
        
        int row_in_bar = bar_row;
        
        if (row_in_bar == 0) {
            /* Label row */
            for (int k = 0; labels[b][k] && label_x + k < GRID_W; k++)
                line[label_x + k] = labels[b][k];
        } else if (row_in_bar >= 1 && row_in_bar <= 4) {
            /* Bar fill */
            int bar_y = row_in_bar - 1;  /* 0..3 */
            int threshold = (bar_y + 1) * bar_len / 4;
            
            for (int bx = 0; bx < bar_len && bar_x + bx < GRID_W; bx++) {
                tork_pixel_t *p = grid_at(g, bar_x + bx, y_global);
                if (!p) continue;
                
                if (bx < filled && bx >= threshold - (bar_len / 4)) {
                    /* Active bar segment */
                    p->r = colors[b][0];
                    p->g = colors[b][1];
                    p->b = colors[b][2];
                    p->brightness = 200;
                } else if (bx < filled) {
                    /* Dimmer filled segment */
                    p->r = (uint8_t)(colors[b][0] / 2);
                    p->g = (uint8_t)(colors[b][1] / 2);
                    p->b = (uint8_t)(colors[b][2] / 2);
                    p->brightness = 100;
                } else {
                    /* Empty */
                    p->r = 10;
                    p->g = 10;
                    p->b = 15;
                    p->brightness = 15;
                }
            }
        } else {
            /* Bottom border */
            for (int bx = 0; bx < bar_len && bar_x + bx < GRID_W; bx++) {
                tork_pixel_t *p = grid_at(g, bar_x + bx, y_global);
                if (!p) continue;
                p->r = 40; p->g = 40; p->b = 50;
                p->brightness = 50;
            }
        }
    }
}

/* Rows 17-22: Branch health */
static void render_branches(tork_grid_t *g, int y_global, char *line, int max_len) {
    (void)max_len;
    const grid_soul_feed_t *s = &g->soul;
    memset(line, ' ', GRID_W);
    line[GRID_W] = '\0';
    
    /* Each branch gets a 10-wide column */
    for (int b = 0; b < 8 && b < s->active_branches + 1; b++) {
        int col_x = b * 10;
        int row_in_branch = y_global - 17;  /* 0..5 */
        
        int8_t bd = s->branch_drive[b];
        uint32_t bt = s->branch_ticks[b];
        
        /* Color based on drive */
        uint8_t r = scale(bd + 128, 0, 255, 10, 255);
        uint8_t g_comp = scale(128 - bd, 0, 255, 10, 100);
        uint8_t b_comp = (uint8_t)(bt > 0 ? 100 : 20);
        
        for (int dx = 0; dx < 8 && col_x + dx < GRID_W; dx++) {
            tork_pixel_t *p = grid_at(g, col_x + dx, y_global);
            if (!p) continue;
            
            if (row_in_branch == 0 || row_in_branch == 5) {
                /* Top/bottom border */
                p->r = 30; p->g = 30; p->b = 40;
                p->brightness = 40;
            } else {
                int bar_height = 4;
                int intensity = scale(bd + 128, 0, 255, 0, bar_height);
                if (row_in_branch <= intensity) {
                    p->r = r; p->g = g_comp; p->b = b_comp;
                    p->brightness = 200;
                } else {
                    p->r = 5; p->g = 5; p->b = 10;
                    p->brightness = 10;
                }
            }
        }
    }
}

/* Rows 24-33: Experience heatmap */
static void render_experience(tork_grid_t *g, int y_global, char *line, int max_len) {
    (void)max_len;
    const grid_soul_feed_t *s = &g->soul;
    memset(line, ' ', GRID_W);
    line[GRID_W] = '\0';
    
    int heat_row = y_global - 24;  /* 0..9 */
    int cols_per_outcome = 5;  /* 16 outcomes * 5 cols = 80 cols */
    
    for (int e = 0; e < 16 && e < s->outcome_count; e++) {
        int ox = e * cols_per_outcome;
        int8_t outcome = s->recent_outcomes[e];
        
        uint8_t r = outcome > 0 ? scale(outcome, 0, 100, 20, 255) : 10;
        uint8_t g_comp = outcome > 0 ? scale(outcome, 0, 100, 10, 100) : 5;
        uint8_t b_comp = outcome < 0 ? scale(-outcome, 0, 100, 5, 100) : 5;
        
        for (int dx = 0; dx < cols_per_outcome && ox + dx < GRID_W; dx++) {
            tork_pixel_t *p = grid_at(g, ox + dx, y_global);
            if (!p) continue;
            
            p->r = r;
            p->g = g_comp;
            p->b = b_comp;
            p->brightness = outcome > 0 ? (uint8_t)(100 + scale(outcome, 0, 100, 0, 155))
                                        : (uint8_t)(50);
        }
    }
    
    /* Label row 0 */
    if (heat_row == 0) {
        int label_x = 0;
        const char *lbl = "EXPERIENCE HEATMAP";
        for (int k = 0; lbl[k] && label_x + k < GRID_W; k++)
            line[label_x + k] = lbl[k];
    }
}

/* ── Initialize ────────────────────────────────────────────── */
void grid_init(tork_grid_t *g) {
    memset(g, 0, sizeof(*g));
    g->global_tick = 0;
    g->paused = 0;
    g->display_mode = 1;  /* Default to soul-viz mode */
    g->waveform_pos = 0;
    
    /* Initialize waveform with zeros */
    memset(g->waveform, 0, sizeof(g->waveform));
    
    /* Initialize soul feed to neutral */
    grid_soul_feed_t *s = &g->soul;
    s->tick = 0;
    s->drive = 0;
    s->fear = 10;
    s->desire = 30;
    s->curiosity = 50;
    s->active_branches = 0;
    s->experience_count = 0;
    
    /* Initialize pixels */
    srand((unsigned int)time(NULL));
    for (int i = 0; i < GRID_N; i++) {
        tork_pixel_t *p = &g->pixels[i];
        p->brightness = 10;
        p->decay_rate = 3;
        p->influence = 20;
        p->curiosity = 0;
        p->tick = 0;
        p->r = 5; p->g = 5; p->b = 10;
    }
}

/* ── Feed real-time soul data ───────────────────────────────── */
void grid_feed_soul(tork_grid_t *g, const grid_soul_feed_t *soul) {
    if (!g || !soul) return;
    memcpy(&g->soul, soul, sizeof(grid_soul_feed_t));
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
            if (n) { sum += n->brightness; count++; }
        }
    }
    return (count > 0) ? (uint8_t)(sum / count) : 0;
}

/* ── One tick ─────────────────────────────────────────────── */
void grid_tick(tork_grid_t *g) {
    if (g->paused) return;
    grid_soul_feed_t *s = &g->soul;
    
    /* ── Update waveform ── */
    /* Map drive + stress to waveform height */
    uint8_t wave_val = scale(s->drive + 128, 0, 255, 0, 200);
    wave_val = (uint8_t)(wave_val + s->hw_stress * 20);
    if (s->tick > 0)
        g->waveform[g->waveform_pos % GRID_W] = wave_val;
    g->waveform_pos++;
    
    /* ── Tick the background emergence pixels (non-viz zones) ── */
    /* Only for pixels outside the dedicated viz zones */
    for (int i = 0; i < GRID_N; i++) {
        int x = i % GRID_W;
        int y = i / GRID_W;
        tork_pixel_t *p = &g->pixels[i];
        
        /* Skip pixels in dedicated viz zones — they're set by renderers */
        /* But we still let them breathe slightly */
        uint8_t nb = grid_neighbor_sum(g, x, y);
        
        /* Spontaneous fluctuation */
        int16_t b_delta = (rand() % 5) - 2;
        b_delta -= p->decay_rate / 3;
        
        int16_t new_b = (int16_t)p->brightness + b_delta;
        p->brightness = clamp16(new_b);
        p->neighbor_sum = nb;
        p->tick++;
    }
    
    g->global_tick++;
}

/* ── Render to terminal ───────────────────────────────────── */
void grid_render(tork_grid_t *g) {
    char line_buf[GRID_W + 1];
    
    for (int y = 0; y < GRID_H; y++) {
        memset(line_buf, 0, sizeof(line_buf));
        
        if (y == 0) {
            /* Status bar */
            render_status_bar(g, line_buf, sizeof(line_buf));
        } else if (y >= 1 && y <= 8) {
            /* Waveform */
            render_waveform(g, y, line_buf, sizeof(line_buf));
        } else if (y >= 10 && y <= 15) {
            /* Instinct bars */
            render_instinct_bars(g, y, line_buf, sizeof(line_buf));
        } else if (y >= 17 && y <= 22) {
            /* Branch health */
            render_branches(g, y, line_buf, sizeof(line_buf));
        } else if (y >= 24 && y <= 33) {
            /* Experience heatmap */
            render_experience(g, y, line_buf, sizeof(line_buf));
        } else if (y == GRID_H - 1) {
            /* Bottom bar */
            snprintf(line_buf, sizeof(line_buf),
                "TRK ♥%05lu | M%d | %s",
                g->global_tick, g->display_mode,
                g->paused ? "PAUSED" : "ALIVE");
        } else {
            /* Background emergence — use pixel values directly */
            for (int x = 0; x < GRID_W; x++) {
                /* Let pixels glow softly */
                tork_pixel_t *p = grid_at(g, x, y);
                if (!p) continue;
                if (p->brightness < 3) {
                    p->brightness = 3;
                    p->r = 2; p->g = 1; p->b = 3;
                }
            }
        }
        
        /* ANSI 24-bit color render */
        for (int x = 0; x < GRID_W; x++) {
            tork_pixel_t *p = grid_at(g, x, y);
            if (!p) { printf(" "); continue; }
            
            char c = line_buf[x] ? line_buf[x] : ' ';
            if (c == ' ') {
                printf("\033[48;2;%d;%d;%dm ", p->r, p->g, p->b);
            } else {
                printf("\033[48;2;%d;%d;%dm\033[38;2;%d;%d;%dm%c",
                       p->r/2, p->g/2, p->b/2,
                       255-p->r/2, 255-p->g/2, 255-p->b/2, c);
            }
        }
        printf("\033[0m\n");
    }
}

/* ── Frame (with cursor reset) ────────────────────────────── */
void grid_frame(tork_grid_t *g) {
    printf("\033[H");
    grid_render(g);
    printf("\033[0m");
    fflush(stdout);
}
