#include "code_reader.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* Locate the start of a function body in assembly text.
   Returns pointer to the line after the function label, or NULL. */
static const char *find_func_start(const char *buf, int len, const char *func_name) {
    int nlen = strlen(func_name);
    const char *p = buf;
    const char *end = buf + len;
    while (p < end) {
        /* find end of line */
        const char *eol = memchr(p, '\n', end - p);
        if (!eol) eol = end;
        int line_len = (int)(eol - p);

        /* check if this line is the target label: "func_name:" */
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

/* Check if a line is a label (ends with ':' before any comment). */
static int is_label_line(const char *line, int len) {
    /* Labels don't start with \t — instructions always do */
    if (len > 0 && line[0] == '\t') return 0;
    for (int i = 0; i < len; i++) {
        if (line[i] == '#') return 0;  /* comment before colon */
        if (line[i] == ':') return 1;
    }
    return 0;
}

/* Check if a line is an assembler directive (starts with '.'). */
static int is_directive_line(const char *line, int len) {
    /* skip leading whitespace */
    int i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i < len && line[i] == '.') return 1;
    if (i < len && line[i] == '#') return 1;
    return 0;
}

/* Extract the opcode from an instruction line.
   Instruction lines start with whitespace then have an opcode.
   Returns the length of the opcode, or 0 if not an instruction.
   Writes the opcode (base form, stripped of suffix) into out[]. */
static int extract_opcode_from_line(const char *line, int len, char *out, int out_size) {
    int i = 0;
    /* skip leading whitespace */
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= len) return 0;

    /* must start with a letter — instruction opcode */
    if (!isalpha((unsigned char)line[i])) return 0;

    /* collect the opcode */
    int j = 0;
    while (i < len && j < out_size - 1) {
        char c = line[i];
        if (isalnum((unsigned char)c) || c == '_') {
            out[j++] = c;
            i++;
        } else {
            break;
        }
    }
    out[j] = '\0';

    /* strip common suffixes: q, l, w, b (but keep if opcode is only 2-3 chars like 'je') */
    int slen = strlen(out);
    if (slen > 3) {
        char last = out[slen - 1];
        if (last == 'q' || last == 'l' || last == 'w' || last == 'b') {
            /* don't strip if it would make it too short or if it's part of the base name */
            /* e.g. "addq" -> "add", "movzbl" -> "movzb" -> keep stripping */
            out[slen - 1] = '\0';
            slen--;
            /* strip again for movzb -> movz etc. */
            if (slen > 3) {
                last = out[slen - 1];
                if (last == 'q' || last == 'l' || last == 'w' || last == 'b') {
                    out[slen - 1] = '\0';
                }
            }
        }
    }
    return (int)strlen(out);
}

/* Parse next line from position p within [buf, buf+len).
   Sets *line_start and *line_len. Returns pointer past the newline. */
static const char *next_line(const char *p, const char *end,
                             const char **line_start, int *line_len) {
    *line_start = p;
    const char *eol = memchr(p, '\n', end - p);
    if (!eol) {
        *line_len = (int)(end - p);
        return end;
    }
    *line_len = (int)(eol - p);
    return (eol < end) ? eol + 1 : end;
}

/* ── Public API ─────────────────────────────────────────────── */

int asm_read_file(const char *path, char *buf, int buf_size) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = (int)fread(buf, 1, buf_size - 1, f);
    fclose(f);
    if (n < 0) return -1;
    buf[n] = '\0';
    return n;
}

int asm_count_insns_in_func(const char *buf, int len, const char *func_name) {
    const char *p = find_func_start(buf, len, func_name);
    if (!p) return -1;
    const char *end = buf + len;
    int count = 0;

    while (p < end) {
        const char *ls;
        int ll;
        p = next_line(p, end, &ls, &ll);
        if (ll == 0) continue;

        /* stop at next non-local label (not starting with .L) */
        if (is_label_line(ls, ll)) {
            /* local labels like .L3: continue */
            if (ll > 1 && ls[0] == '.' && ls[1] == 'L') continue;
            /* any other label = next function or end */
            break;
        }

        /* skip directives and comments */
        if (is_directive_line(ls, ll)) continue;

        /* skip blank/whitespace-only */
        int blank = 1;
        for (int i = 0; i < ll; i++) {
            if (!isspace((unsigned char)ls[i])) { blank = 0; break; }
        }
        if (blank) continue;

        /* remaining lines with leading tab are instructions */
        if (ls[0] == '\t') count++;
    }
    return count;
}

int asm_extract_opcodes(const char *buf, int len, const char *func_name,
                        char opcodes[][8], int max_count) {
    const char *p = find_func_start(buf, len, func_name);
    if (!p) return -1;
    const char *end = buf + len;
    int count = 0;

    while (p < end && count < max_count) {
        const char *ls;
        int ll;
        p = next_line(p, end, &ls, &ll);
        if (ll == 0) continue;

        if (is_label_line(ls, ll)) {
            if (ll > 1 && ls[0] == '.' && ls[1] == 'L') continue;
            break;
        }
        if (is_directive_line(ls, ll)) continue;

        char opc[16];
        int olen = extract_opcode_from_line(ls, ll, opc, sizeof(opc));
        if (olen > 0) {
            snprintf(opcodes[count], 8, "%.7s", opc);
            count++;
        }
    }
    return count;
}

int asm_classify_insns(const char *buf, int len, const char *func_name,
                       int *count_mov, int *count_arith,
                       int *count_control, int *count_other) {
    *count_mov = *count_arith = *count_control = *count_other = 0;
    const char *p = find_func_start(buf, len, func_name);
    if (!p) return -1;
    const char *end = buf + len;
    int total = 0;

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

        char opc[16];
        int olen = extract_opcode_from_line(ls, ll, opc, sizeof(opc));
        if (olen == 0) continue;
        total++;

        /* classify */
        if (strcmp(opc, "mov") == 0 || strcmp(opc, "movz") == 0 ||
            strcmp(opc, "movs") == 0 || strcmp(opc, "lea") == 0 ||
            strcmp(opc, "xchg") == 0 || strcmp(opc, "push") == 0 ||
            strcmp(opc, "pop") == 0 ||
            /* cmov* variants: cmovz, cmovnz, cmovge, cmovl, cmova, cmovbe, etc. */
            strncmp(opc, "cmov", 4) == 0 ||
            /* movz* movs* variants */
            strncmp(opc, "movzb", 5) == 0 || strncmp(opc, "movsb", 5) == 0 ||
            strncmp(opc, "movzw", 5) == 0 || strncmp(opc, "movsw", 5) == 0 ||
            strncmp(opc, "movzl", 5) == 0 || strncmp(opc, "movsl", 5) == 0) {
            (*count_mov)++;
        } else if (strcmp(opc, "add") == 0 || strcmp(opc, "sub") == 0 ||
                   strcmp(opc, "mul") == 0 || strcmp(opc, "imul") == 0 ||
                   strcmp(opc, "div") == 0 || strcmp(opc, "idiv") == 0 ||
                   strcmp(opc, "inc") == 0 || strcmp(opc, "dec") == 0 ||
                   strcmp(opc, "neg") == 0 || strcmp(opc, "not") == 0 ||
                   strcmp(opc, "and") == 0 || strcmp(opc, "or") == 0 ||
                   strcmp(opc, "xor") == 0 || strcmp(opc, "sh") == 0 ||
                   strcmp(opc, "shr") == 0 || strcmp(opc, "shl") == 0 ||
                   strcmp(opc, "sar") == 0 || strcmp(opc, "sal") == 0 ||
                   strcmp(opc, "adc") == 0 || strcmp(opc, "sbb") == 0 ||
                   strcmp(opc, "ror") == 0 || strcmp(opc, "rol") == 0 ||
                   /* comparison/test (flags only, no mov) */
                   strcmp(opc, "cmp") == 0 || strcmp(opc, "test") == 0 ||
                   /* setcc: sete, setne, setl, setge, etc. */
                   strncmp(opc, "set", 3) == 0) {
            (*count_arith)++;
        } else if (strcmp(opc, "jmp") == 0 || strcmp(opc, "je") == 0 ||
                   strcmp(opc, "jne") == 0 || strcmp(opc, "jz") == 0 ||
                   strcmp(opc, "jnz") == 0 || strcmp(opc, "jg") == 0 ||
                   strcmp(opc, "jl") == 0 || strcmp(opc, "jge") == 0 ||
                   strcmp(opc, "jle") == 0 || strcmp(opc, "ja") == 0 ||
                   strcmp(opc, "jb") == 0 || strcmp(opc, "jae") == 0 ||
                   strcmp(opc, "jbe") == 0 || strcmp(opc, "js") == 0 ||
                   strcmp(opc, "jns") == 0 || strcmp(opc, "jo") == 0 ||
                   /* jcc long forms */
                   strncmp(opc, "j", 1) == 0 ||
                   strcmp(opc, "call") == 0 || strcmp(opc, "ret") == 0 ||
                   strcmp(opc, "loop") == 0) {
            (*count_control)++;
        } else {
            (*count_other)++;
        }
    }
    return total;
}
