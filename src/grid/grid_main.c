/* ── TORK Grid Emergence — Standalone Display ────────────────
 *  Compile: gcc -Igrid -o build/tork_grid grid/grid_main.c grid/tork_grid.c -lrt
 *  Run:     ./build/tork_grid [frames]          # Autonomous emergence
 *           ./build/tork_grid --live [frames]   # Live soul visualization
 * ──────────────────────────────────────────────────────────── */

#include "tork_grid.h"
#include "grid_soul_connector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

static tork_grid_t grid;
static volatile int running = 1;
static int live_mode = 0;

void handle_sigint(int sig) {
    (void)sig;
    running = 0;
}

int main(int argc, char **argv) {
    int frames = 500;
    int sleep_ms = 50;  /* 20fps */
    
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--live") == 0 || strcmp(argv[a], "-l") == 0)
            live_mode = 1;
        else {
            int v = atoi(argv[a]);
            if (v > 0) frames = v;
        }
    }
    if (frames <= 0) frames = 500;
    
    signal(SIGINT, handle_sigint);
    
    /* ── Clear screen, hide cursor ── */
    printf("\033[2J\033[?25l");
    printf("\033[1;1mTRK Grid %s  [Ctrl+C to exit]%s\033[0m\n",
           live_mode ? "LIVE SOUL VIZ" : "AUTONOMOUS EMERGENCE",
           live_mode ? " (connect to running engine)" : "");
    
    grid_init(&grid);
    
    /* ── Live mode: connect to engine's shared memory ── */
    if (live_mode) {
        if (grid_viewer_init() != 0) {
            printf("\033[2J\033[?25h");
            fprintf(stderr, "❌ Engine not running. Start engine first.\n");
            return 1;
        }
        grid.display_mode = 1;
        printf("   Connected to engine soul data\n");
        sleep_ms = 100;  /* 10fps for live mode (less CPU) */
    } else {
        grid.display_mode = 0;
        printf("   Autonomous mode — no engine needed\n");
    }
    
    struct timespec ts;
    ts.tv_sec = sleep_ms / 1000;
    ts.tv_nsec = (sleep_ms % 1000) * 1000000L;
    
    for (int f = 0; f < frames && running; f++) {
        if (live_mode) {
            /* ── Read latest soul data from engine ── */
            grid_soul_feed_t feed;
            if (grid_viewer_read(&feed) == 0 && feed.tick > 0) {
                grid_feed_soul(&grid, &feed);
            }
        }
        
        grid_tick(&grid);
        grid_frame(&grid);
        nanosleep(&ts, NULL);
    }
    
    /* ── Cleanup ── */
    printf("\033[?25h");
    if (live_mode) grid_viewer_cleanup();
    printf("\nGrid finished after %lu ticks\n", grid.global_tick);
    
    return 0;
}
