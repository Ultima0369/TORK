#include "codegen.h"
#include "../sandbox/sandbox.h"
#include "../code/code_reader.h"
#include "../code/code_modifier.h"
#include "dispatch.h"
#include "../learning/experience.h"
#include "../learning/pattern.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── 内部状态 ────────────────────────────────────────────── */
static codegen_template_t g_templates[CODEGEN_MAX_TEMPLATE];
static int g_tmpl_count = 0;
static int g_initialized = 0;

/* ── 初始化 ──────────────────────────────────────────────── */
void codegen_init(void) {
    memset(g_templates, 0, sizeof(g_templates));
    g_tmpl_count = 0;
    g_initialized = 1;

    /* 注册内置模板: memcpy 字节循环 */
    codegen_register_template("memcpy_byte_loop",
        "\t.file\t\"codegen.c\"\n"
        "\t.text\n"
        "\t.p2align 4\n"
        "\t.globl\tmemcpy_tork\n"
        "\t.type\tmemcpy_tork, @function\n"
        "memcpy_tork:\n"
        "\t.cfi_startproc\n"
        "\tmovq\t%rdi, %rax\n"
        "\ttestq\t%rdx, %rdx\n"
        "\tje\t.L2\n"
        "\txorl\t%ecx, %ecx\n"
        ".L3:\n"
        "\tmovzbl\t(%rsi,%rcx), %r8d\n"
        "\tmovb\t%r8b, (%rax,%rcx)\n"
        "\taddq\t$1, %rcx\n"
        "\tcmpq\t%rcx, %rdx\n"
        "\tjne\t.L3\n"
        ".L2:\n"
        "\tret\n"
        "\t.cfi_endproc\n"
        "\t.size\tmemcpy_tork, .-memcpy_tork\n"
        "\t.section\t.note.GNU-stack,\"\",@progbits\n"
    );

    /* memcpy 4字节循环 */
    codegen_register_template("memcpy_word_loop",
        "\t.file\t\"codegen.c\"\n"
        "\t.text\n"
        "\t.p2align 4\n"
        "\t.globl\tmemcpy_tork\n"
        "\t.type\tmemcpy_tork, @function\n"
        "memcpy_tork:\n"
        "\t.cfi_startproc\n"
        "\tmovq\t%rdi, %rax\n"
        "\tmovq\t%rdx, %rcx\n"
        "\tshrq\t$2, %rcx\n"
        "\ttestq\t%rcx, %rcx\n"
        "\tje\t.L_tail\n"
        "\txorl\t%r8d, %r8d\n"
        ".L4:\n"
        "\tmovl\t(%rsi,%r8,4), %r9d\n"
        "\tmovl\t%r9d, (%rax,%r8,4)\n"
        "\tincq\t%r8\n"
        "\tcmpq\t%r8, %rcx\n"
        "\tjne\t.L4\n"
        ".L_tail:\n"
        "\tandq\t$3, %rdx\n"
        "\tje\t.L_done\n"
        "\tleaq\t(%rsi,%rcx,4), %rsi\n"
        "\tleaq\t(%rax,%rcx,4), %rdi\n"
        "\txorl\t%ecx, %ecx\n"
        ".L5:\n"
        "\tmovzbl\t(%rsi,%rcx), %r8d\n"
        "\tmovb\t%r8b, (%rdi,%rcx)\n"
        "\tincq\t%rcx\n"
        "\tcmpq\t%rcx, %rdx\n"
        "\tjne\t.L5\n"
        ".L_done:\n"
        "\tmovq\t%rax, %rdi\n"
        "\tret\n"
        "\t.cfi_endproc\n"
        "\t.size\tmemcpy_tork, .-memcpy_tork\n"
        "\t.section\t.note.GNU-stack,\"\",@progbits\n"
    );

    printf("  CODEGEN: initialized with %d templates\n", g_tmpl_count);
}

/* ── 注册模板 ────────────────────────────────────────────── */
int codegen_register_template(const char *name, const char *asm_src) {
    if (!name || !asm_src || g_tmpl_count >= CODEGEN_MAX_TEMPLATE) return -1;

    codegen_template_t *t = &g_templates[g_tmpl_count];
    snprintf(t->name, CODEGEN_MAX_NAME, "%s", name);
    t->src_len = snprintf(t->src, CODEGEN_MAX_SRC, "%s", asm_src);
    t->num_params = 0;
    g_tmpl_count++;
    return 0;
}

/* ── 查找模板 ────────────────────────────────────────────── */
const codegen_template_t *codegen_find_template(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < g_tmpl_count; i++) {
        if (strcmp(g_templates[i].name, name) == 0)
            return &g_templates[i];
    }
    return NULL;
}

/* ── 变异策略名称 ────────────────────────────────────────── */
static const char *variant_name(variant_strategy_t s) {
    static const char *names[] = {
        "none", "reg_swap", "unroll_2", "unroll_4",
        "align_change", "branch_hint", "movzx_opt", "nop_pad"
    };
    if (s >= 0 && s < VAR_NUM_STRATEGIES) return names[s];
    return "unknown";
}

/* ── 应用变异策略 ──────────────────────────────────────────
 * 从模板源码出发，按策略修改生成新变体
 */
int codegen_apply_variant(const codegen_template_t *tmpl,
                           variant_strategy_t strategy,
                           codegen_variant_t *out) {
    if (!tmpl || !out || strategy <= VAR_NONE || strategy >= VAR_NUM_STRATEGIES)
        return -1;

    memset(out, 0, sizeof(codegen_variant_t));
    out->strategy = strategy;

    /* 从模板复制源码 */
    int len = tmpl->src_len;
    if (len >= CODEGEN_MAX_SRC) len = CODEGEN_MAX_SRC - 1;
    memcpy(out->src, tmpl->src, len);
    out->src[len] = '\0';
    out->src_len = len;

    snprintf(out->name, CODEGEN_MAX_NAME, "%s_%s", tmpl->name, variant_name(strategy));

    char *src = out->src;
    int mod = 0;

    switch (strategy) {

    case VAR_REG_SWAP: {
        /* 交换 %r8d ↔ 一个空闲寄存器: 用 %r9d 替代 %r8d */
        char *p = strstr(src, "%r8d");
        while (p) {
            memcpy(p, "%r9d", 4);
            p = strstr(p + 4, "%r8d");
            mod++;
        }
        p = strstr(src, "%r8b");
        while (p) {
            memcpy(p, "%r9b", 4);
            p = strstr(p + 4, "%r8b");
            mod++;
        }
        break;
    }

    case VAR_UNROLL_2: {
        /* 展开 2 份: 第一份用 %r8b offset0, 第二份用 %r9b offset1 */
        char *loop_start = strstr(src, ".L3:\n");
        if (!loop_start) { loop_start = strstr(src, ".L3:\r\n"); }
        if (!loop_start) return -1;

        char *jne = strstr(loop_start, "\tjne\t");
        if (!jne) jne = strstr(loop_start, "\tjne ");
        if (!jne) return -1;

        char *line_end = strchr(jne, '\n');
        if (!line_end) return -1;

        char *body_start = loop_start + 5;
        int body_len = (int)(jne - body_start);

        char body_buf[CODEGEN_MAX_SRC];
        memcpy(body_buf, body_start, body_len);
        body_buf[body_len] = '\0';

        char new_src[CODEGEN_MAX_SRC];
        int prefix_len = (int)(body_start - src);
        memcpy(new_src, src, prefix_len);
        int ni = prefix_len;

        /* 第一份: movzbl (%rsi,%rcx), %r8d / movb %r8b, (%rax,%rcx) — 原样 */
        memcpy(new_src + ni, body_buf, body_len);
        ni += body_len;

        /* 第二份: movzbl 1(%rsi,%rcx), %r9d / movb %r9b, 1(%rax,%rcx) */
        char body1[CODEGEN_MAX_SRC];
        memcpy(body1, body_buf, body_len + 1);
        /* 替换 (%rsi,%rcx) → 1(%rsi,%rcx) */
        char *p = strstr(body1, "(%rsi,%rcx)");
        if (p) {
            int old_len = 11; const char *off = "1(%rsi,%rcx)"; int new_len = 12;
            char tmp[CODEGEN_MAX_SRC];
            int pi = (int)(p - body1);
            memcpy(tmp, body1, pi);
            memcpy(tmp + pi, off, new_len);
            int rest = (int)strlen(body1) - pi - old_len;
            memcpy(tmp + pi + new_len, p + old_len, rest + 1);
            memcpy(body1, tmp, pi + new_len + rest + 1);
        }
        /* 替换 (%rax,%rcx) → 1(%rax,%rcx) */
        p = strstr(body1, "(%rax,%rcx)");
        if (p) {
            int old_len = 11; const char *off = "1(%rax,%rcx)"; int new_len = 12;
            char tmp[CODEGEN_MAX_SRC];
            int pi = (int)(p - body1);
            memcpy(tmp, body1, pi);
            memcpy(tmp + pi, off, new_len);
            int rest = (int)strlen(body1) - pi - old_len;
            memcpy(tmp + pi + new_len, p + old_len, rest + 1);
            memcpy(body1, tmp, pi + new_len + rest + 1);
        }
        /* 替换 %r8d → %r9d, %r8b → %r9b */
        p = body1;
        while ((p = strstr(p, "%r8d"))) { memcpy(p, "%r9d", 4); p += 4; }
        p = body1;
        while ((p = strstr(p, "%r8b"))) { memcpy(p, "%r9b", 4); p += 4; }

        int b1_len = (int)strlen(body1);
        memcpy(new_src + ni, body1, b1_len);
        ni += b1_len;

        const char *add2 = "\taddq\t$2, %rcx\n";
        int add2_len = (int)strlen(add2);
        memcpy(new_src + ni, add2, add2_len);
        ni += add2_len;

        int jne_line_len = (int)(line_end - jne + 1);
        memcpy(new_src + ni, jne, jne_line_len);
        ni += jne_line_len;

        int tail_len = out->src_len - (int)(line_end - src) - 1;
        if (tail_len > 0) {
            memcpy(new_src + ni, line_end + 1, tail_len);
            ni += tail_len;
        }
        new_src[ni] = '\0';
        memcpy(src, new_src, ni + 1);
        out->src_len = ni;
        mod = 1;
        break;
    }

    case VAR_UNROLL_4: {
        /* 对 byte_loop: 重写为 movl 4字节循环 (不是4份byte展开)
         * 对 word_loop: 不适用，返回 -1 */
        if (!strstr(src, "movzbl")) return -1; /* 非byte循环不适用 */

        char *loop_start = strstr(src, ".L3:\n");
        if (!loop_start) return -1;

        /* 重写循环体: movl (%rsi,%rcx), %r8d / movl %r8d, (%rax,%rcx) / addq $4 */
        const char *new_body =
            ".L3:\n"
            "\tmovl\t(%rsi,%rcx), %r8d\n"
            "\tmovl\t%r8d, (%rax,%rcx)\n"
            "\taddq\t$4, %rcx\n"
            "\tcmpq\t%rcx, %rdx\n"
            "\tjne\t.L3\n";

        /* 替换从 .L3: 到 ret 之间的所有内容 */
        char *ret_label = strstr(loop_start, ".L2:\n");
        if (!ret_label) return -1;

        int prefix_len = (int)(loop_start - src);
        char new_src[CODEGEN_MAX_SRC];
        memcpy(new_src, src, prefix_len);
        int ni = prefix_len;

        int body_len = (int)strlen(new_body);
        memcpy(new_src + ni, new_body, body_len);
        ni += body_len;

        /* 拼接 .L2 之后的内容 (ret + cfi + size) */
        int tail_len = out->src_len - (int)(ret_label - src);
        if (tail_len > 0 && ni + tail_len < CODEGEN_MAX_SRC) {
            memcpy(new_src + ni, ret_label, tail_len);
            ni += tail_len;
        }
        new_src[ni] = '\0';
        memcpy(src, new_src, ni + 1);
        out->src_len = ni;
        mod = 1;
        break;
    }

    case VAR_ALIGN_CHANGE: {
        /* 更改 .p2align 值 */
        char *p = src;
        while ((p = strstr(p, ".p2align"))) {
            char *num_start = p + 8;
            while (*num_start == ' ' || *num_start == '\t') num_start++;
            if (*num_start >= '0' && *num_start <= '9') {
                /* 递增对齐值 (最大 6) */
                int val = *num_start - '0';
                if (val < 6) {
                    *num_start = '0' + val + 1;
                    mod++;
                }
            }
            p += 8;
        }
        break;
    }

    case VAR_BRANCH_HINT: {
        /* 将 jne 替换为 jne 加 nop 按照分支预测 */
        char *p = strstr(src, "\tjne\t");
        if (p) {
            char tmp[CODEGEN_MAX_SRC];
            int prefix_len = (int)(p - src);
            memcpy(tmp, src, prefix_len);
            int ni = prefix_len;
            /* 添加分支提示: 假设 taken，在 jne 前添加 align */
            const char *hint = "\t.p2align 2\n";
            int hlen = (int)strlen(hint);
            memcpy(tmp + ni, hint, hlen);
            ni += hlen;
            int rest_len = out->src_len - prefix_len;
            memcpy(tmp + ni, p, rest_len);
            ni += rest_len;
            tmp[ni] = '\0';
            memcpy(src, tmp, ni + 1);
            out->src_len = ni;
            mod = 1;
        }
        break;
    }

    case VAR_MOVZX_OPT: {
        /* 优化循环计数: addq $1 → incq (1 byte shorter) */
        char *p = strstr(src, "addq\t$1, %rcx");
        if (p) {
            const char *new_insn = "incq\t%rcx";
            int old_len = 14;
            int new_len = (int)strlen(new_insn);
            char tmp[CODEGEN_MAX_SRC];
            int prefix_len = (int)(p - src);
            memcpy(tmp, src, prefix_len);
            memcpy(tmp + prefix_len, new_insn, new_len);
            int rest_start = prefix_len + old_len;
            int rest_len = out->src_len - rest_start;
            if (rest_len > 0)
                memcpy(tmp + prefix_len + new_len, src + rest_start, rest_len);
            int total = prefix_len + new_len + rest_len;
            tmp[total] = '\0';
            memcpy(src, tmp, total + 1);
            out->src_len = total;
            mod = 1;
        }
        break;
    }

    case VAR_NOP_PAD: {
        /* 在函数入口添加 NOP 填充 */
        char *func_start = strstr(src, "memcpy_tork:\n");
        if (!func_start) func_start = strstr(src, "memcpy_tork:\r\n");
        if (func_start) {
            char tmp[CODEGEN_MAX_SRC];
            int prefix_len = (int)(func_start - src) + 13; /* skip "memcpy_tork:\n" */
            memcpy(tmp, src, prefix_len);
            const char *nops = "\tnop\n\tnop\n\tnop\n\tnop\n";
            int nops_len = (int)strlen(nops);
            memcpy(tmp + prefix_len, nops, nops_len);
            int rest_len = out->src_len - prefix_len;
            memcpy(tmp + prefix_len + nops_len, src + prefix_len, rest_len);
            int total = prefix_len + nops_len + rest_len;
            tmp[total] = '\0';
            memcpy(src, tmp, total + 1);
            out->src_len = total;
            mod = 1;
        }
        break;
    }

    default:
        return -1;
    }

    if (mod == 0) return -1;

    snprintf(out->name + strlen(out->name), CODEGEN_MAX_NAME - strlen(out->name),
             "_v%d", mod);
    return 0;
}

/* ── 编译验证 ──────────────────────────────────────────────
 * 用 as 汇编 + ld 链接验证变体是否可编译
 */
int codegen_compile_verify(codegen_variant_t *variant, const char *work_dir) {
    if (!variant || variant->src_len <= 0) return -1;

    const char *dir = (work_dir && work_dir[0]) ? work_dir : "/tmp/tork_codegen";
    char cmd[2048];
    char sfile[512], ofile[512];

    snprintf(sfile, sizeof(sfile), "%s/%s.s", dir, variant->name);
    snprintf(ofile, sizeof(ofile), "%s/%s.o", dir, variant->name);

    /* 写源码文件 */
    FILE *f = fopen(sfile, "w");
    if (!f) return -1;
    fwrite(variant->src, 1, variant->src_len, f);
    fclose(f);

    /* as -o ... */
    snprintf(cmd, sizeof(cmd), "mkdir -p %s && as -o %s %s 2>&1", dir, ofile, sfile);
    sandbox_result_t sr = sandbox_exec(cmd, 10);
    variant->compile_ok = (sr.exit_code == 0);

    return variant->compile_ok ? 0 : -1;
}

/* ── Benchmark ─────────────────────────────────────────────
 * as 编译 .o → gcc 整体链接 harness → 运行计时 + 正确性校验
 */
int codegen_benchmark(codegen_variant_t *variant, const char *work_dir,
                       int iterations, int size) {
    if (!variant || !variant->compile_ok) return -1;

    const char *dir = (work_dir && work_dir[0]) ? work_dir : "/tmp/tork_codegen";
    char cmd[4096];
    char sfile[512], ofile[512], cfile[512], binfile[512];

    snprintf(sfile, sizeof(sfile), "%s/%s.s", dir, variant->name);
    snprintf(ofile, sizeof(ofile), "%s/%s.o", dir, variant->name);
    snprintf(cfile, sizeof(cfile), "%s/bench_%s.c", dir, variant->name);
    snprintf(binfile, sizeof(binfile), "%s/bench_%s", dir, variant->name);

    /* 写汇编源码 */
    FILE *f = fopen(sfile, "w");
    if (!f) return -1;
    fwrite(variant->src, 1, variant->src_len, f);
    fclose(f);

    /* 生成 benchmark harness — 包含正确性校验 */
    const char *harness =
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <time.h>\n"
        "extern void *memcpy_tork(void*, const void*, unsigned long);\n"
        "int main(int argc, char **argv) {\n"
        "    int iters = argc > 1 ? atoi(argv[1]) : 1000;\n"
        "    int sz = argc > 2 ? atoi(argv[2]) : 4096;\n"
        "    char *dst = malloc(sz);\n"
        "    char *src = malloc(sz);\n"
        "    for (int i = 0; i < sz; i++) src[i] = (char)i;\n"
        "    /* correctness check */\n"
        "    memset(dst, 0, sz);\n"
        "    memcpy_tork(dst, src, sz);\n"
        "    int bad = 0;\n"
        "    for (int i = 0; i < sz; i++) if (dst[i] != src[i]) bad++;\n"
        "    if (bad > 0) { printf(\"FAIL %d\\n\", bad); return 1; }\n"
        "    /* benchmark */\n"
        "    struct timespec t0, t1;\n"
        "    clock_gettime(CLOCK_MONOTONIC, &t0);\n"
        "    for (int i = 0; i < iters; i++)\n"
        "        memcpy_tork(dst, src, sz);\n"
        "    clock_gettime(CLOCK_MONOTONIC, &t1);\n"
        "    long ns = (t1.tv_sec - t0.tv_sec)*1000000000L + (t1.tv_nsec - t0.tv_nsec);\n"
        "    printf(\"%ld\\n\", ns / iters);\n"
        "    free(dst); free(src);\n"
        "    return 0;\n"
        "}\n";

    f = fopen(cfile, "w");
    if (!f) return -1;
    fputs(harness, f);
    fclose(f);

    /* 编译: as → gcc (直接链接 .o + harness) */
    snprintf(cmd, sizeof(cmd),
        "cd %s && "
        "as -o %s %s 2>/dev/null && "
        "gcc -O2 -o %s %s %s -lrt 2>/dev/null",
        dir, ofile, sfile, binfile, cfile, ofile);

    sandbox_result_t sr = sandbox_exec(cmd, 30);
    if (sr.exit_code != 0) return -1;

    /* 运行 benchmark */
    int it = iterations > 0 ? iterations : 1000;
    int sz = size > 0 ? size : 4096;
    snprintf(cmd, sizeof(cmd), "%s %d %d", binfile, it, sz);
    sr = sandbox_exec(cmd, 10);
    if (sr.exit_code != 0) return -1;

    variant->benchmark_ns = atoi(sr.stdout_buf);

    /* 计算得分: 越低越好，相对于基线 */
#define MEMCPY_BASELINE_NS 500
    int baseline = MEMCPY_BASELINE_NS;
    if (variant->benchmark_ns > 0) {
        variant->score = (float)baseline / (float)variant->benchmark_ns;
    }

    return 0;
}

/* ── MCTS 驱动的代码搜索 ──────────────────────────────────
 * 对指定模板尝试所有变异策略，编译验证 + benchmark，
 * 返回得分最高的变体
 */
int codegen_mcts_search(const char *template_name,
                          codegen_variant_t *best_out,
                          int time_budget_ms) {
    if (!template_name || !best_out) return -1;

    const codegen_template_t *tmpl = codegen_find_template(template_name);
    if (!tmpl) return -1;

    (void)time_budget_ms; /* 简化: 穷举所有策略 */

    codegen_variant_t best;
    memset(&best, 0, sizeof(best));
    best.score = -1e9f;
    int found = 0;

    for (int s = VAR_NONE + 1; s < VAR_NUM_STRATEGIES; s++) {
        codegen_variant_t v;
        if (codegen_apply_variant(tmpl, (variant_strategy_t)s, &v) != 0)
            continue;

        if (codegen_compile_verify(&v, NULL) != 0)
            continue;

        v.compile_ok = 1;

        if (codegen_benchmark(&v, NULL, 500, 4096) == 0 && v.benchmark_ns > 0) {
            if (v.score > best.score) {
                best = v;
                found = 1;
            }
            printf("  CODEGEN: %s score=%.3f bench=%dns\n",
                   v.name, v.score, v.benchmark_ns);
        }
    }

    if (found) {
        *best_out = best;
        return 0;
    }
    return -1;
}

/* ── 统计 ────────────────────────────────────────────────── */
int codegen_template_count(void) {
    return g_tmpl_count;
}

/* ── 清理 ────────────────────────────────────────────────── */
void codegen_cleanup(void) {
    printf("  CODEGEN: %d templates registered\n", g_tmpl_count);
    g_tmpl_count = 0;
    g_initialized = 0;
}
