#ifndef PERSISTOR_H
#define PERSISTOR_H

#include <stdint.h>
#include <stddef.h>

#define PERSIST_DIR  "/tmp/tork/persist/"

/* Save bb/params/rules to disk. soul_buf/soul_len save soul data (may be NULL/0). */
int ps_save_all(const void *soul_buf, size_t soul_len);

/* Restore all shared memory regions from disk. Returns count of files restored. */
int ps_restore_all(void);

/* Restore soul data into provided buffer. Returns bytes read, or 0 on failure. */
size_t ps_restore_soul(void *buf, size_t len);

/* Decay: forget low-value rules and prune stale data. */
int ps_decay_memory(void);

/* Hot-swap: save state, fork+exec new binary, parent exits on success. */
int ps_hot_swap(const char *new_binary_path);

/* Signal-safe: save and exit (call from SIGTERM handler). */
void ps_emergency_save(void);

/* Register soul snapshot buffer for emergency_save (avoids reading unmapped 0x200000). */
void ps_register_soul_buf(const void *buf, size_t len);

/* Mark that cal_init() has completed (PARAM_ADDR is now mapped). */
void ps_mark_cal_initialized(void);

/* Cleanup .bak files. */
void ps_cleanup_baks(void);

#endif
