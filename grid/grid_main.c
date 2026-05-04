/* ── TORK Grid Emergence — Standalone Display ────────────────
 *  Compile: gcc -Igrid -o build/tork_grid grid/grid_main.c grid/tork_grid.c
 *  Run:     ./build/tork_grid [frames]
 * ──────────────────────────────────────────────────────────── */

#include "tork_grid.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

static tork_grid_t grid;
static volatile int running = 1;

void handle_sigint(int sig) {
    (void)sig;
    running = 0;
}

int main(int argc, char **argv) {
    int frames = (argc > 1) ? atoi(argv[1]) : 500;
    if (frames <= 0) frames = 500;
    
    signal(SIGINT, handle_sigint);
    
    /* Clear screen and hide cursor */
    printf("\033[2J\033[?25l");
    
    grid_init(&grid);
    
    /* Display info line */
    printf("\033[2;1mT🥚RK Grid Emergence  [Ctrl+C to exit]\033[0m\n");
    
    struct timespec ts = {0, 50000000};  /* 50ms per frame (~20fps) */
    
    for (int f = 0; f < frames && running; f++) {
        grid_tick(&grid);
        grid_frame(&grid);
        nanosleep(&ts, NULL);
    }
    
    /* Show cursor again */
    printf("\033[?25h");
    printf("\nGrid finished after %lu ticks\n", grid.global_tick);
    
    return 0;
}
