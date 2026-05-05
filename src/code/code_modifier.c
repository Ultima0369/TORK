#include "code_modifier.h"
#include "code_reader.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/wait.h>

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
    /* Labels don't start with \t — instructions always do */
    if (len > 0 && line[0] == '\t') return 0;
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

int asm_replace_operand(char *buf, int len, int buf_capacity, const char *func_name,
                        const char *old_op, const char *new_op,
                        int occurrence, int *new_len) {
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

        for (int i = 0; i <= ll - olen; i++) {
            if (strncmp(ls + i, old_op, olen) == 0) {
                found++;
                if (found == occurrence) {
                    int pos = (int)(ls - buf) + i;
                    int diff = nlen - olen;
                    if (len + diff > buf_capacity) return -1;
                    if (diff != 0)
                        memmove(buf + pos + nlen, buf + pos + olen, len - pos - olen);
                    memcpy(buf + pos, new_op, nlen);
                    int updated_len = len + diff;
                    if (new_len) *new_len = updated_len;
                    return 1;
                }
            }
        }
    }
    if (new_len) *new_len = len;
    return 0;
}

int asm_verify_modification(const char *buf, int len, const char *work_dir) {
    char path[256];
    snprintf(path, sizeof(path), "%s/tmp.s", work_dir);

    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fwrite(buf, 1, len, f);
    fclose(f);

    char obj_path[256];
    snprintf(obj_path, sizeof(obj_path), "%s/tmp.o", work_dir);

    pid_t pid = fork();
    if (pid < 0) { unlink(path); return 0; }
    if (pid == 0) {
        execl("/usr/bin/as", "as", "-o", obj_path, path, NULL);
        _exit(1);
    }
    int st;
    waitpid(pid, &st, 0);

    unlink(obj_path);
    unlink(path);

    return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 1 : 0;
}

int asm_rollback(char *buf, int len, const char *backup, int backup_len) {
    int restore = (backup_len < len) ? backup_len : len;
    memcpy(buf, backup, restore);
    return restore;
}

/* Check if a line is a ret instruction (ret, retq, retl) */
static int is_ret_insn(const char *line, int len) {
    int i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    int j = i;
    while (j < len && isalpha((unsigned char)line[j])) j++;
    int wlen = j - i;
    if (wlen == 3 && strncmp(line + i, "ret", 3) == 0) return 1;
    if (wlen == 4 && strncmp(line + i, "retq", 4) == 0) return 1;
    if (wlen == 4 && strncmp(line + i, "retl", 4) == 0) return 1;
    return 0;
}

/* Check if a line is an actual instruction (tab-indented, not directive/label/blank) */
static int is_insn_line(const char *line, int len) {
    if (len == 0) return 0;
    if (line[0] != '\t') return 0;
    /* skip the leading tab */
    int i = 1;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= len) return 0;
    if (line[i] == '.' || line[i] == '#') return 0;
    return isalpha((unsigned char)line[i]);
}

/* Check if a line is a NOP instruction (nop, nopw, nopl, nopq, or .byte nop encoding) */
static int is_nop_insn(const char *line, int len) {
    int i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= len) return 0;

    /* nop variants: nop, nopw, nopl, nopq (may have operands after) */
    if (i + 3 <= len && strncmp(line + i, "nop", 3) == 0) {
        char suf = (i + 3 < len) ? line[i + 3] : '\0';
        /* bare nop: followed by nothing, whitespace, or newline */
        if (suf == '\0' || suf == '\n' || suf == '\r' || suf == ' ' || suf == '\t')
            return 1;
        /* nopw/nopl/nopq: may be followed by operands */
        if (suf == 'w' || suf == 'l' || suf == 'q')
            return 1;
    }

    /* .byte nop encodings:
       - 0x90 → 1-byte nop
       - 0x66, 0x90 → 2-byte nop (0x66 prefix + 0x90 nop)
       Other .byte sequences are NOT nops */
    if (i + 5 <= len && strncmp(line + i, ".byte", 5) == 0) {
        const char *p = line + i + 5;
        while (p < line + len && (*p == ' ' || *p == '\t')) p++;
        /* .byte 0x90 — single-byte nop */
        if (p + 4 <= line + len && strncmp(p, "0x90", 4) == 0) {
            const char *after = p + 4;
            while (after < line + len && (*after == ' ' || *after == '\t')) after++;
            if (after >= line + len || *after == '\n' || *after == '\r')
                return 1;
        }
        /* .byte 0x66, 0x90 — two-byte nop */
        if (p + 4 < line + len && strncmp(p, "0x66", 4) == 0) {
            const char *after = p + 4;
            while (after < line + len && (*after == ' ' || *after == '\t' || *after == ',')) after++;
            if (after + 4 <= line + len && strncmp(after, "0x90", 4) == 0)
                return 1;
        }
    }

    return 0;
}

int asm_delete_nop_insns(char *buf, int len, const char *func_name, int *new_len) {
    const char *p = find_func_start(buf, len, func_name);
    if (!p) { if (new_len) *new_len = len; return -1; }
    const char *end = buf + len;
    int deleted = 0;

    while (p < end) {
        const char *ls;
        int ll;
        const char *next = next_line(p, end, &ls, &ll);

        /* stop at next non-local label (function boundary) */
        if (is_label_line(ls, ll)) {
            if (!(ll > 1 && ls[0] == '.' && ls[1] == 'L'))
                break;
            p = next;
            continue;
        }

        /* skip directives and blanks */
        if (is_directive_line(ls, ll)) { p = next; continue; }

        if (is_nop_insn(ls, ll)) {
            int line_total = (int)(next - ls);
            int pos = (int)(ls - buf);
            memmove(buf + pos, buf + pos + line_total, len - pos - line_total);
            len -= line_total;
            end = buf + len;
            deleted++;
            p = buf + pos;
            continue;
        }

        p = next;
    }

    buf[len] = '\0';
    if (new_len) *new_len = len;
    return deleted;
}

int asm_delete_dead_insns(char *buf, int len, const char *func_name, int *new_len) {
    const char *p = find_func_start(buf, len, func_name);
    if (!p) { if (new_len) *new_len = len; return -1; }
    const char *end = buf + len;
    int found_ret = 0;
    int deleted = 0;
    int bytes_removed = 0;

    while (p < end) {
        const char *ls;
        int ll;
        const char *next = next_line(p, end, &ls, &ll);

        /* stop at next non-local label (function boundary) */
        if (is_label_line(ls, ll)) {
            if (!(ll > 1 && ls[0] == '.' && ls[1] == 'L'))
                break;
            p = next;
            continue;
        }

        /* skip directives and blanks */
        if (!found_ret && is_ret_insn(ls, ll)) {
            found_ret = 1;
            p = next;
            continue;
        }

        if (found_ret && is_insn_line(ls, ll)) {
            int line_total = (int)(next - ls);
            int pos = (int)(ls - buf);
            memmove(buf + pos, buf + pos + line_total, len - pos - line_total);
            len -= line_total;
            bytes_removed += line_total;
            end = buf + len;
            deleted++;
            p = buf + pos;
            continue;
        }

        p = next;
    }

    /* null-terminate the shortened buffer */
    buf[len] = '\0';

    if (new_len) *new_len = len;
    return deleted;
}