#include "dispatch.h"
#include "codegen.h"
#include "../sandbox/sandbox.h"
#include "../code/code_reader.h"
#include "../code/code_modifier.h"
#include "auditor.h"
#include "fission.h"
#include "task.h"
#include "../learning/experience.h"
#include "../learning/pattern.h"
#include <stdio.h>
#include <string.h>

/* ── 统计 ──────────────────────────────────────────────── */
static uint32_t g_total_calls = 0;
static uint32_t g_total_success = 0;
static uint32_t g_total_fail = 0;

/* ── Action 名称表 ─────────────────────────────────────── */
static const char *g_action_names[] = {
    "self_modify", "self_optimize", "self_mcts_mod", "self_fission",
    "exec_cmd", "analyze_asm", "audit_code",
    "codegen_search", "codegen_compile", "codegen_bench",
};

/* ── 初始化 ─────────────────────────────────────────────── */
void dispatch_init(void) {
    g_total_calls = 0;
    g_total_success = 0;
    g_total_fail = 0;
    printf("  DISPATCH: unified tool dispatch layer initialized\n");
}

/* ── 计算经验 outcome ────────────────────────────────────
 * 从 dispatch 结果推导一个 experience outcome 值
 * 正值 = 好，负值 = 坏，0 = 无变化
 */
static int8_t compute_outcome(const dispatch_output_t *out, dispatch_action_t action) {
    if (out->rc != 0) return -30;           /* 执行失败 */

    switch (action) {
    case DISP_EXEC_CMD:
        return (out->exit_code == 0) ? 20 : -15;
    case DISP_ANALYZE_ASM:
    case DISP_AUDIT_CODE:
        return (out->output_len > 0) ? 15 : -10;
    case DISP_SELF_MODIFY:
    case DISP_SELF_OPTIMIZE:
    case DISP_SELF_MCTS_MOD:
        return (out->compile_ok) ? 60 : -20;
    case DISP_CODEGEN_SEARCH:
        return (out->score > 0.0f) ? 40 : -20;
    case DISP_CODEGEN_COMPILE:
        return (out->compile_ok) ? 30 : -15;
    case DISP_CODEGEN_BENCH:
        return (out->benchmark_ns > 0) ? 25 : -10;
    case DISP_SELF_FISSION:
        return (out->rc == 0) ? 10 : -20;
    default:
        return 0;
    }
}

/* ── 统一调度入口 ──────────────────────────────────────── */
dispatch_output_t tork_dispatch(const dispatch_input_t *in) {
    dispatch_output_t out;
    memset(&out, 0, sizeof(out));
    out.rc = -1;

    if (!in || !in->input) return out;

    g_total_calls++;

    switch (in->action) {

    /* ── 外部命令执行 ── */
    case DISP_EXEC_CMD: {
        sandbox_result_t sr = sandbox_exec(in->input, in->timeout_sec > 0 ? in->timeout_sec : 30);
        out.rc = 0;
        out.exit_code = sr.exit_code;
        out.output_len = snprintf(out.output, sizeof(out.output),
            "{\"exit_code\":%d,\"timed_out\":%s,\"stdout_len\":%d,\"stderr_len\":%d}",
            sr.exit_code, sr.timed_out ? "true" : "false",
            (int)strlen(sr.stdout_buf), (int)strlen(sr.stderr_buf));
        /* Append stdout/stderr only if they fit */
        int remaining = (int)sizeof(out.output) - out.output_len - 1;
        if (remaining > 100) {
            int stdout_len = (int)strlen(sr.stdout_buf);
            int stderr_len = (int)strlen(sr.stderr_buf);
            int overhead = 40; /* separators */
            int avail = remaining - overhead;
            if (avail > 0) {
                int stdout_copy = (stdout_len < avail / 2) ? stdout_len : avail / 2;
                int stderr_copy = (stderr_len < avail - stdout_copy) ? stderr_len : avail - stdout_copy;
                out.output_len += snprintf(out.output + out.output_len,
                    sizeof(out.output) - out.output_len,
                    "\n--- stdout ---\n%.*s\n--- stderr ---\n%.*s",
                    stdout_copy, sr.stdout_buf, stderr_copy, sr.stderr_buf);
            }
        }
        if (sr.exit_code == 0) g_total_success++;
        else g_total_fail++;
        break;
    }

    /* ── 汇编文件分析 ── */
    case DISP_ANALYZE_ASM: {
        char asm_buf[8192];
        int alen = asm_read_file(in->input, asm_buf, sizeof(asm_buf));
        if (alen <= 0) {
            out.rc = -1;
            snprintf(out.output, sizeof(out.output), "{\"error\":\"cannot read %s\"}", in->input);
            g_total_fail++;
            break;
        }
        const char *fn = in->func_name ? in->func_name : "memcpy_tork";
        int cm = 0, ca = 0, cc = 0, co = 0;
        asm_classify_insns(asm_buf, alen, fn, &cm, &ca, &cc, &co);
        int insns = asm_count_insns_in_func(asm_buf, alen, fn);
        out.rc = 0;
        out.output_len = snprintf(out.output, sizeof(out.output),
            "{\"file\":\"%s\",\"func\":\"%s\",\"insns\":%d,\"mov\":%d,\"arith\":%d,\"ctrl\":%d,\"other\":%d}",
            in->input, fn, insns, cm, ca, cc, co);
        g_total_success++;
        break;
    }

    /* ── 代码审计 ── */
    case DISP_AUDIT_CODE: {
        audit_result_t ar = audit_asm_file(in->input, in->func_name);
        out.rc = 0;
        out.output_len = audit_result_to_json(&ar, out.output, sizeof(out.output));
        out.score = ar.risk_score;
        g_total_success++;
        break;
    }

    /* ── MCTS 代码修改 ── */
    case DISP_SELF_MCTS_MOD: {
        if (!in->func_name || !in->input) {
            out.rc = -1;
            g_total_fail++;
            break;
        }
        char asm_buf[8192];
        int alen = asm_read_file(in->input, asm_buf, sizeof(asm_buf));
        if (alen <= 0) {
            out.rc = -1;
            g_total_fail++;
            break;
        }
        char backup[8192];
        memcpy(backup, asm_buf, alen);

        /* 从 input 解析修改类型 — input 格式: "modify_type:param1:param2"
         * modify_type = replace_op | del_dead | del_nop | swap_regs */
        int mod_ok = 0;
        int new_len = alen;
        if (strncmp(in->func_name, "replace_op:", 11) == 0) {
            /* replace_op:old:new — 如 replace_op:\tje\t:\tjz\t */
            const char *old_str = in->func_name + 11;
            const char *colon2 = strchr(old_str, ':');
            if (colon2) {
                char old_val[64], new_val[64];
                int olen = (int)(colon2 - old_str);
                if (olen >= (int)sizeof(old_val)) olen = sizeof(old_val) - 1;
                memcpy(old_val, old_str, olen);
                old_val[olen] = '\0';
                snprintf(new_val, sizeof(new_val), "%s", colon2 + 1);
                int rep = asm_replace_operand(asm_buf, alen, (int)sizeof(asm_buf), "memcpy_tork", old_val, new_val, 1, &new_len);
                if (rep > 0) {
                    if (asm_verify_modification(asm_buf, new_len, "benchmark/memcpy")) {
                        mod_ok = 1;
                    }
                }
            }
        } else if (strcmp(in->func_name, "del_dead") == 0) {
            int del = asm_delete_dead_insns(asm_buf, alen, "memcpy_tork", &new_len);
            if (del > 0 && asm_verify_modification(asm_buf, new_len, "benchmark/memcpy"))
                mod_ok = 1;
        } else if (strcmp(in->func_name, "del_nop") == 0) {
            int nops = asm_delete_nop_insns(asm_buf, alen, "memcpy_tork", &new_len);
            if (nops > 0 && asm_verify_modification(asm_buf, new_len, "benchmark/memcpy"))
                mod_ok = 1;
        } else if (strcmp(in->func_name, "swap_regs") == 0) {
            int rep = asm_replace_operand(asm_buf, alen, (int)sizeof(asm_buf), "memcpy_tork", "%rax", "%rdx", 1, &new_len);
            if (rep > 0 && asm_verify_modification(asm_buf, new_len, "benchmark/memcpy"))
                mod_ok = 1;
        }

        if (mod_ok) {
            FILE *f = fopen(in->input, "w");
            if (f) { fwrite(asm_buf, 1, new_len, f); fclose(f); }
            out.rc = 0;
            out.compile_ok = 1;
            out.output_len = snprintf(out.output, sizeof(out.output),
                "{\"action\":\"%s\",\"verified\":true,\"len_before\":%d,\"len_after\":%d}",
                in->func_name, alen, new_len);
            g_total_success++;
        } else {
            asm_rollback(asm_buf, sizeof(asm_buf), backup, alen);
            out.rc = -1;
            out.compile_ok = 0;
            out.output_len = snprintf(out.output, sizeof(out.output),
                "{\"action\":\"%s\",\"verified\":false}", in->func_name);
            g_total_fail++;
        }
        break;
    }

    /* ── 代码自修改 (je→jz 等) ── */
    case DISP_SELF_MODIFY: {
        if (!in->input) { out.rc = -1; g_total_fail++; break; }
        char asm_buf[8192];
        int alen = asm_read_file(in->input, asm_buf, sizeof(asm_buf));
        if (alen <= 0) { out.rc = -1; g_total_fail++; break; }
        char backup[8192];
        memcpy(backup, asm_buf, alen);
        const char *fn = in->func_name ? in->func_name : "memcpy_tork";
        int new_len = alen;
        int rep = asm_replace_operand(asm_buf, alen, (int)sizeof(asm_buf), fn, "\tje\t", "\tjz\t", 1, &new_len);
        if (rep > 0 && asm_verify_modification(asm_buf, new_len, "benchmark/memcpy")) {
            FILE *f = fopen(in->input, "w");
            if (f) { fwrite(asm_buf, 1, new_len, f); fclose(f); }
            out.rc = 0;
            out.compile_ok = 1;
            g_total_success++;
        } else {
            asm_rollback(asm_buf, sizeof(asm_buf), backup, alen);
            out.rc = -1;
            g_total_fail++;
        }
        break;
    }

    /* ── 代码优化 (死代码/NOP删除) ── */
    case DISP_SELF_OPTIMIZE: {
        if (!in->input) { out.rc = -1; g_total_fail++; break; }
        char asm_buf[8192];
        int alen = asm_read_file(in->input, asm_buf, sizeof(asm_buf));
        if (alen <= 0) { out.rc = -1; g_total_fail++; break; }
        char backup[8192];
        memcpy(backup, asm_buf, alen);
        const char *fn = in->func_name ? in->func_name : "memcpy_tork";
        int new_len = alen;
        int deleted = asm_delete_dead_insns(asm_buf, alen, fn, &new_len);
        if (deleted <= 0) {
            /* 尝试 NOP 删除 */
            int nops = asm_delete_nop_insns(asm_buf, alen, fn, &new_len);
            if (nops > 0 && asm_verify_modification(asm_buf, new_len, "benchmark/memcpy")) {
                FILE *f = fopen(in->input, "w");
                if (f) { fwrite(asm_buf, 1, new_len, f); fclose(f); }
                out.rc = 0;
                out.compile_ok = 1;
                g_total_success++;
            } else {
                asm_rollback(asm_buf, sizeof(asm_buf), backup, alen);
                out.rc = -1;
                g_total_fail++;
            }
        } else if (asm_verify_modification(asm_buf, new_len, "benchmark/memcpy")) {
            FILE *f = fopen(in->input, "w");
            if (f) { fwrite(asm_buf, 1, new_len, f); fclose(f); }
            out.rc = 0;
            out.compile_ok = 1;
            g_total_success++;
        } else {
            asm_rollback(asm_buf, sizeof(asm_buf), backup, alen);
            out.rc = -1;
            g_total_fail++;
        }
        break;
    }

    /* ── codegen: MCTS 搜索最优变体 ── */
    case DISP_CODEGEN_SEARCH: {
        codegen_variant_t best;
        if (codegen_mcts_search(in->input, &best, in->iterations > 0 ? in->iterations : 500) == 0) {
            out.rc = 0;
            out.score = best.score;
            out.benchmark_ns = best.benchmark_ns;
            out.compile_ok = best.compile_ok;
            out.output_len = snprintf(out.output, sizeof(out.output),
                "{\"template\":\"%s\",\"best\":\"%s\",\"score\":%.3f,\"bench_ns\":%d,\"compile_ok\":%d}",
                in->input, best.name, best.score, best.benchmark_ns, best.compile_ok);
            g_total_success++;
        } else {
            out.rc = -1;
            out.output_len = snprintf(out.output, sizeof(out.output),
                "{\"error\":\"codegen search failed for template '%s'\"}", in->input);
            g_total_fail++;
        }
        break;
    }

    /* ── codegen: 编译验证 ── */
    case DISP_CODEGEN_COMPILE: {
        /* input = 模板名, func_name = 变异策略名(可选) */
        const codegen_template_t *tmpl = codegen_find_template(in->input);
        if (!tmpl) {
            out.rc = -1;
            out.output_len = snprintf(out.output, sizeof(out.output),
                "{\"error\":\"template '%s' not found\"}", in->input);
            g_total_fail++;
            break;
        }

        variant_strategy_t strategy = VAR_REG_SWAP;
        if (in->func_name) {
            for (int s = VAR_NONE + 1; s < VAR_NUM_STRATEGIES; s++) {
                codegen_variant_t tmp;
                codegen_apply_variant(tmpl, (variant_strategy_t)s, &tmp);
                if (strstr(tmp.name, in->func_name)) {
                    strategy = (variant_strategy_t)s;
                    break;
                }
            }
        }

        codegen_variant_t v;
        if (codegen_apply_variant(tmpl, strategy, &v) != 0) {
            out.rc = -1;
            out.output_len = snprintf(out.output, sizeof(out.output),
                "{\"error\":\"variant apply failed\"}");
            g_total_fail++;
            break;
        }

        if (codegen_compile_verify(&v, in->work_dir) == 0) {
            out.rc = 0;
            out.compile_ok = 1;
            out.output_len = snprintf(out.output, sizeof(out.output),
                "{\"variant\":\"%s\",\"compile_ok\":true}", v.name);
            g_total_success++;
        } else {
            out.rc = -1;
            out.compile_ok = 0;
            out.output_len = snprintf(out.output, sizeof(out.output),
                "{\"variant\":\"%s\",\"compile_ok\":false}", v.name);
            g_total_fail++;
        }
        break;
    }

    /* ── codegen: benchmark ── */
    case DISP_CODEGEN_BENCH: {
        const codegen_template_t *tmpl = codegen_find_template(in->input);
        if (!tmpl) {
            out.rc = -1;
            out.output_len = snprintf(out.output, sizeof(out.output),
                "{\"error\":\"template '%s' not found\"}", in->input);
            g_total_fail++;
            break;
        }

        variant_strategy_t strategy = VAR_UNROLL_2;
        if (in->func_name) {
            for (int s = VAR_NONE + 1; s < VAR_NUM_STRATEGIES; s++) {
                codegen_variant_t tmp;
                codegen_apply_variant(tmpl, (variant_strategy_t)s, &tmp);
                if (strstr(tmp.name, in->func_name)) {
                    strategy = (variant_strategy_t)s;
                    break;
                }
            }
        }

        codegen_variant_t v;
        if (codegen_apply_variant(tmpl, strategy, &v) != 0 ||
            codegen_compile_verify(&v, in->work_dir) != 0) {
            out.rc = -1;
            out.output_len = snprintf(out.output, sizeof(out.output),
                "{\"error\":\"variant compile failed\"}");
            g_total_fail++;
            break;
        }

        int iters = in->iterations > 0 ? in->iterations : 1000;
        if (codegen_benchmark(&v, in->work_dir, iters, 4096) == 0) {
            out.rc = 0;
            out.benchmark_ns = v.benchmark_ns;
            out.score = v.score;
            out.output_len = snprintf(out.output, sizeof(out.output),
                "{\"variant\":\"%s\",\"bench_ns\":%d,\"score\":%.3f}",
                v.name, v.benchmark_ns, v.score);
            g_total_success++;
        } else {
            out.rc = -1;
            out.output_len = snprintf(out.output, sizeof(out.output),
                "{\"error\":\"benchmark failed\"}");
            g_total_fail++;
        }
        break;
    }

    /* ── 自分裂 ── */
    case DISP_SELF_FISSION: {
        pid_t pid = fission_spawn();
        if (pid > 0) {
            out.rc = 0;
            out.output_len = snprintf(out.output, sizeof(out.output),
                "{\"fission\":\"spawned\",\"child_pid\":%d}", (int)pid);
            g_total_success++;
        } else {
            out.rc = -1;
            out.output_len = snprintf(out.output, sizeof(out.output),
                "{\"fission\":\"failed\"}");
            g_total_fail++;
        }
        break;
    }

    default:
        out.rc = -1;
        out.output_len = snprintf(out.output, sizeof(out.output),
            "{\"error\":\"unknown action %d\"}", in->action);
        g_total_fail++;
        break;
    }

    /* ── 闭环关键：将 (action, result) 写入 experience ──
     * 所有行为经此回流，不再丢失 */
    out.exp_outcome = compute_outcome(&out, in->action);
    uint8_t act_code = (uint8_t)in->action;
    uint8_t crash = (out.rc != 0) ? 1 : 0;
    uint8_t compile = out.compile_ok ? 1 : 0;
    exp_record(
        in->tick, in->hw_stress, in->drive, (uint16_t)in->gen_count,
        act_code, 0,          /* action, param */
        out.exp_outcome, crash, compile,
        in->hw_stress, in->drive
    );
    out.exp_id = exp_count();

    /* 成功行为 → 模式学习触发 (每 10 次成功) */
    if (out.rc == 0 && g_total_success % 10 == 0) {
        pat_learn_from_experience();
    }

    return out;
}

/* ── Action 名称 ──────────────────────────────────────── */
const char *dispatch_action_name(dispatch_action_t action) {
    if (action >= 0 && action < DISP_NUM_ACTIONS)
        return g_action_names[action];
    return "unknown";
}

/* ── 统计 ──────────────────────────────────────────────── */
uint32_t dispatch_total_calls(void)  { return g_total_calls; }
uint32_t dispatch_total_success(void) { return g_total_success; }
uint32_t dispatch_total_fail(void)    { return g_total_fail; }

/* ── 清理 ──────────────────────────────────────────────── */
void dispatch_cleanup(void) {
    printf("  DISPATCH: %u calls, %u success, %u fail\n",
           g_total_calls, g_total_success, g_total_fail);
}