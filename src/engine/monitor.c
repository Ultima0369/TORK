#include "monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int monitor_parse_proc_status(pid_t pid, const char *field, uint32_t *out) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    size_t flen = strlen(field);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, field, flen) == 0) {
            *out = (uint32_t)strtoul(line + flen, NULL, 10);
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return -1;
}
