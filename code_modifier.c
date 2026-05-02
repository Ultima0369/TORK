#include "code_modifier.h"
#include "code_reader.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Locate function body start (same logic as code_reader) */
static const char *find_func_start(const char *buf, int len, const char *func_name) {
    int nlen = strlen(func_name);
    const char *p = buf;
    const char *end = buf + len;
    while (p < end) {
        const char *eol = memchr(p, '\n', end - p);
        if (!eol) eol = end;
        int line_len = (int)(eol - p);
        if (line_len >= nlen + 1
            && p[nlen] == ':'
            && strncmp(p, func_name, nlen) == 0
            && (line_len == nlen + 1 || p[nlen + 1] == '\n' || p[nlen + 1] == '\r')) {
            return (eol < end) ? eol + 1 : eol;
        }
        p = (eol < end) ? eol + 1 : end;
    }
    return NULL;
}

static int is_label_line(const char *line, int len) {
    for (int i = 0; i < len; i++) {
        if (line[i] == '#') return 0;
        if (line[i] == ':') return 1;
    }
    return 0;
}

static int is_directive_line(const char *line, int len) {
    int i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i < len && line[i] == '.') return 1;
    if (i < len && line[i] == '#') return 1;
    return 0;
}

static const char *next_line(const char *p, const char *end,
                             const char **ls, int *ll) {
    *ls = p;
    const char *eol = memchr(p, '\n', end - p);
    if (!eol) { *ll = (int)(end - p); return end; }
    *ll = (int)(eol - p);
    return (eol < end) ? eol + 1 : end;
}

/* ── Public API ────────────────────────────────────────────── */

int asm_replace_operand(char *buf, int len, const char *func_name,
                        const char *old_op, const char *new_op,
                        int occurrence) {
    const char *p = find_func_start(buf, len, func_name);
    if (!p) return -1;
    const char *end = buf + len;
    int olen = strlen(old_op);
    int nlen = strlen(new_op);
    int found = 0;

    while (p < end) {
        const char *ls;
        int ll;
        p = next_line(p, end, &ls, &ll);
        if (ll == 0) continue;

        if (is_label_line(ls, ll)) {
            if (ll > 1 && ls[0] == '.' && ls[1] == 'L') continue;
            break;
        }
        if (is_directive_line(ls, ll)) continue;

        /* search for old_op on this line */
        for (int i = 0; i <= ll - olen; i++) {
            if (strncmp(ls + i, old_op, olen) == 0) {
                found++;
                if (found == occurrence) {
                    /* perform replacement in the mutable buffer */
                    int pos = (int)(ls - buf) + i;
                    int diff = nlen - olen;
                    if (diff != 0)
                        memmove(buf + pos + nlen, buf + pos + olen, len - pos - olen);
                    memcpy(buf + pos, new_op, nlen);
                    /* caller must track new length if diff != 0 */
                    return 1;
                }
            }
        }
    }
    return 0;
}

int asm_verify_modification(const char *buf, int len, const char *work_dir) {
    char path[256];
    snprintf(path, sizeof(path), "%s/tmp.s", work_dir);

    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fwrite(buf, 1, len, f);
    fclose(f);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "as -o %s/tmp.o %s/tmp.s 2>/dev/null", work_dir, work_dir);
    int rc = system(cmd);

    snprintf(cmd, sizeof(cmd), "%s/tmp.o", work_dir);
    unlink(cmd);
    snprintf(cmd, sizeof(cmd), "%s/tmp.s", work_dir);
    unlink(cmd);

    return (rc == 0) ? 1 : 0;
}

int asm_rollback(char *buf, int len, const char *backup, int backup_len) {
    int restore = (backup_len < len) ? backup_len : len;
    memcpy(buf, backup, restore);
    return restore;
}