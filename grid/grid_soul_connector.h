#ifndef GRID_SOUL_CONNECTOR_H
#define GRID_SOUL_CONNECTOR_H

#include "tork_grid.h"

/* Engine side: initialize shared memory */
int grid_engine_init(void);

/* Engine side: write soul data (call each tick) */
void grid_engine_write(const grid_soul_feed_t *soul);

/* Engine side: cleanup */
void grid_engine_cleanup(void);

/* Grid viewer side: open shared memory */
int grid_viewer_init(void);

/* Grid viewer side: read latest soul data */
int grid_viewer_read(grid_soul_feed_t *out);

/* Grid viewer side: cleanup */
void grid_viewer_cleanup(void);

#endif /* GRID_SOUL_CONNECTOR_H */
