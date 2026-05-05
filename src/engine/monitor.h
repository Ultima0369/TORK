#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>
#include <sys/types.h>

/* Read an integer field from /proc/pid/status (e.g. "PPid:\t") */
int monitor_parse_proc_status(pid_t pid, const char *field, uint32_t *out);

#endif
