#include "auditor.h"
#include "code_reader.h"
#include "../learning/pi_index.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── 内部辅助 ────────────────────────────────────────────── */

static void add_risk(audit_result_t *r, risk_level_t level, const char *desc) {
    if (r->risk_count >= AUDIT_MAX_RISKS) return;
    r->risks[r->risk_count].level = level;
    snprintf(r->risks[r->risk_count].desc, sizeof(r->risks[0].desc), "%s", desc);
    r->risk_count++;
}

static const char *risk_label(risk_level_t lv) {
    switch (lv) {
    case RISK_LOW:      return "LOW";
    case RISK_MEDIUM:   return "MEDIUM";
    case RISK_HIGH:     return "HIGH";
    case RISK_CRITICAL: return "CRITICAL";
    default:            return "NONE";
    }
}

/* ── 审计汇编文件 ────────────────────────────────────────── */

audit_result_t audit_asm_file(const char *filepath, const char *func_name) {
    audit_result_t r;
    memset(&r, 0, sizeof(r));
    snprintf(r.filepath, sizeof(r.filepath), "%s", filepath ? filepath : "");
    snprintf(r.func_name, sizeof(r.func_name), "%s", func_name ? func_name : "unknown");

    /* 1. 读取文件 */
    char asm_buf[16384];
    int alen = asm_read_file(filepath, asm_buf, sizeof(asm_buf));
    if (alen <= 0) {
        add_risk(&r, RISK_CRITICAL, "cannot read file");
        r.risk_score = 100.0f;
        return r;
    }

    /* 2. 用 π 指纹标记这份代码的振动特征 */
    r.profile = pi_hash_profile((const uint8_t *)asm_buf, alen);

    /* 3. 指令分类 */
    int cm = 0, ca = 0, cc = 0, co = 0;
    asm_classify_insns(asm_buf, alen, func_name, &cm, &ca, &cc, &co);
    r.mov_count = cm;
    r.arith_count = ca;
    r.ctrl_count = cc;
    r.other_count = co;

    /* 4. 统计 NOP */
    char opcodes[64][8];
    int max_op = asm_extract_opcodes(asm_buf, alen, func_name, opcodes, 64);
    int nop_count = 0;
    for (int i = 0; i < max_op; i++) {
        if (strcmp(opcodes[i], "nop") == 0 || strcmp(opcodes[i], "nopw") == 0 ||
            strcmp(opcodes[i], "nopl") == 0)
            nop_count++;
    }
    r.nop_count = nop_count;

    /* 5. 指令总数 */
    r.total_insns = asm_count_insns_in_func(asm_buf, alen, func_name);
    if (r.total_insns <= 0) r.total_insns = cm + ca + cc + co;

    /* 6. 控制流比例 */
    int total = r.total_insns > 0 ? r.total_insns : 1;
    r.ctrl_ratio = (float)r.ctrl_count / (float)total;

    /* ── 风险检测 ──────────────────────────────────────────── */

    /* 6a. NOP 填充冗余 */
    if (nop_count > total / 4 && nop_count > 2) {
        add_risk(&r, RISK_LOW, "excessive NOP padding — likely alignment filler");
    } else if (nop_count > 0) {
        add_risk(&r, RISK_NONE, "minor NOP padding detected");
    }

    /* 6b. 控制流比例过高 → 复杂度高 */
    if (r.ctrl_ratio > 0.5f) {
        add_risk(&r, RISK_HIGH, "control-flow ratio > 50% — high complexity, consider simplification");
    } else if (r.ctrl_ratio > 0.35f) {
        add_risk(&r, RISK_MEDIUM, "control-flow ratio > 35% — moderate complexity");
    }

    /* 6c. 函数体过大 */
    if (total > 100) {
        add_risk(&r, RISK_HIGH, "function body > 100 instructions — consider splitting");
    } else if (total > 50) {
        add_risk(&r, RISK_MEDIUM, "function body > 50 instructions — may benefit from extraction");
    }

    /* 6d. 死代码检测：ret 后的不可达指令 */
    {
        /* 在汇编文本中找 ret 指令，检查后面是否还有非空行 */
        const char *p = asm_buf;
        int after_ret = 0;
        int dead_insns = 0;
        while (*p) {
            if (after_ret) {
                /* 跳过空行和标签 */
                while (*p == '\n' || *p == '\r' || *p == '\t' || *p == ' ') p++;
                if (*p == '.' || *p == '_') { p++; continue; }
                if (*p && *p != '\n') {
                    dead_insns++;
                    while (*p && *p != '\n') p++;
                }
            } else {
                /* 找 ret 指令 */
                if ((p[0] == 'r' || p[0] == 'R') &&
                    (p[1] == 'e' || p[1] == 'E') &&
                    (p[2] == 't' || p[2] == 'T') &&
                    (p[3] == '\n' || p[3] == '\r' || p[3] == ' ' || p[3] == '\0' || p[3] == '#')) {
                    after_ret = 1;
                }
                while (*p && *p != '\n') p++;
            }
            if (*p == '\n') p++;
        }
        r.dead_code_blocks = dead_insns > 0 ? 1 : 0;
        if (dead_insns > 3) {
            add_risk(&r, RISK_HIGH, "dead code after ret — unreachable instructions detected");
        } else if (dead_insns > 0) {
            add_risk(&r, RISK_MEDIUM, "minor dead code after ret");
        }
    }

    /* 6e. 算术指令过少 → 可能缺少实质计算 */
    if (total > 10 && ca < 2) {
        add_risk(&r, RISK_LOW, "very few arithmetic instructions — mostly data movement?");
    }

    /* 6f. 与 π 索引中已知风险的振动相似度 */
    {
        pi_match_t m = pidx_query(&r.profile, 0.7f);
        if (m.slot_idx >= 0 && m.category == 1) {
            /* category=1 是 TORK 标记的"有问题代码" */
            char desc[256];
            snprintf(desc, sizeof(desc),
                "π-profile matches known-risk code (similarity=%.2f, ref_id=%u)",
                m.similarity, m.ref_id);
            add_risk(&r, RISK_MEDIUM, desc);
        }
    }

    /* ── 评分 ──────────────────────────────────────────────── */

    /* 复杂度评分：控制流比例 + 函数大小 的综合 */
    r.complexity_score = r.ctrl_ratio * 50.0f +
                         (total > 50 ? (float)(total - 50) * 0.5f : 0.0f);
    if (r.complexity_score > 100.0f) r.complexity_score = 100.0f;

    /* 风险评分：累加各级风险的权重 */
    float risk_sum = 0.0f;
    for (int i = 0; i < r.risk_count; i++) {
        switch (r.risks[i].level) {
        case RISK_LOW:      risk_sum += 5.0f;  break;
        case RISK_MEDIUM:   risk_sum += 15.0f; break;
        case RISK_HIGH:     risk_sum += 30.0f; break;
        case RISK_CRITICAL: risk_sum += 50.0f; break;
        default: break;
        }
    }
    r.risk_score = risk_sum > 100.0f ? 100.0f : risk_sum;

    /* 把审计结果也索引到 π 空间，方便后续识别同类代码 */
    /* category: 0=安全, 1=有风险 */
    uint8_t cat = (r.risk_score > 30.0f) ? 1 : 0;
    pidx_add(&r.profile, (uint32_t)r.risk_score, cat);

    return r;
}

/* ── 序列化为 JSON ────────────────────────────────────────── */

int audit_result_to_json(const audit_result_t *r, char *buf, int buf_size) {
    if (!r || !buf || buf_size <= 0) return -1;

    int off = 0;
    off += snprintf(buf + off, buf_size - off,
        "{\"file\":\"%s\",\"func\":\"%s\","
        "\"insns\":%d,\"mov\":%d,\"arith\":%d,\"ctrl\":%d,\"other\":%d,\"nop\":%d,"
        "\"ctrl_ratio\":%.3f,\"complexity\":%.1f,\"risk_score\":%.1f,"
        "\"dead_code\":%d,"
        "\"risks\":[",
        r->filepath, r->func_name,
        r->total_insns, r->mov_count, r->arith_count, r->ctrl_count,
        r->other_count, r->nop_count,
        r->ctrl_ratio, r->complexity_score, r->risk_score,
        r->dead_code_blocks);

    for (int i = 0; i < r->risk_count; i++) {
        if (i > 0) off += snprintf(buf + off, buf_size - off, ",");
        off += snprintf(buf + off, buf_size - off,
            "{\"level\":\"%s\",\"desc\":\"%s\"}",
            risk_label(r->risks[i].level), r->risks[i].desc);
    }

    off += snprintf(buf + off, buf_size - off, "],");

    /* π 指纹的 hex 表示 */
    off += snprintf(buf + off, buf_size - off, "\"pi_profile\":\"");
    for (int i = 0; i < PI_PROFILE_SIZE; i++)
        off += snprintf(buf + off, buf_size - off, "%02x", r->profile.digest[i]);
    off += snprintf(buf + off, buf_size - off, "\"}");

    return off;
}
