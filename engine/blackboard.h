#ifndef BLACKBOARD_H
#define BLACKBOARD_H

#include <stdint.h>

/* Blackboard layout constants */
#define BB_BASE        0x300000
#define BB_SIZE        4096
#define BB_MAGIC       0x544F524B  /* "TORK" */
#define BB_VERSION     1

#define BB_OFF_MAGIC         0x000
#define BB_OFF_VERSION       0x004
#define BB_OFF_ENTRY_COUNT   0x008
#define BB_OFF_WRITE_CURSOR  0x00C

#define BB_ENTRY_START       0x010
#define BB_ENTRY_END         0x3F0
#define BB_ENTRY_SIZE        16
#define BB_MAX_ENTRIES       ((BB_ENTRY_END - BB_ENTRY_START) / BB_ENTRY_SIZE) /* 62 */

#define BB_OFF_TOTAL_OPT     0x3F0
#define BB_OFF_TOTAL_FISS    0x3F4
#define BB_OFF_TOTAL_ERR     0x3F8

/* Entry types */
#define BB_TYPE_OPT_SUCCESS  1
#define BB_TYPE_OPT_FAIL     2
#define BB_TYPE_TEMP_EVENT   3
#define BB_TYPE_INSTINCT     4
#define BB_TYPE_FISSION      5

/* Initialize blackboard at 0x300000. Returns 0 on success. */
int bb_init(void);

/* Set current tick for subsequent bb_write calls. */
void bb_set_tick(uint32_t tick);

/* Write an entry. Returns slot index, or -1 on failure. */
int bb_write(uint8_t type, uint8_t value, uint32_t payload);

/* Read all entries via callback. */
typedef void (*bb_callback_t)(int index, uint32_t tick, uint16_t instance,
                               uint8_t type, uint8_t value, uint32_t payload);
int bb_read_all(bb_callback_t callback);

/* Global statistics */
uint32_t bb_global_optimizations(void);
uint32_t bb_global_fissions(void);
uint32_t bb_global_errors(void);

/* Increment global counters */
void bb_inc_optimizations(void);
void bb_inc_fissions(void);
void bb_inc_errors(void);

/* Cleanup */
void bb_cleanup(void);

#endif
