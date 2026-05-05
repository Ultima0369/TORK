#include "query.h"
#include "soul_access.h"
#include "../instinct/instinct.h"
#include "../learning/experience.h"
#include "../learning/pattern.h"
#include "../learning/branch.h"
#include "../learning/observer.h"
#include "../learning/snapshot.h"
#include "../learning/energy.h"
#include "../learning/watcher.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ── 简单字符串匹配 ────────────────────────────────────────── */
static int contains(const char *text, const char *keyword) {
    return strstr(text, keyword) != NULL;
}

static void to_lower(char *s) {
    for (; *s; s++) *s = tolower((unsigned char)*s);
}

/* ── 处理状态查询 ──────────────────────────────────────────── */
static void cmd_status(void *soul_ptr, char *resp, int max_len) {
    soul_t *s = (soul_t *)soul_ptr;
    
    snprintf(resp, max_len,
        "TORK AI engine running\n"
        "   心跳: %u tick\n"
        "   驱动: %+d (正向=好奇, 负向=恐惧)\n"
        "   压力: %u\n"
        "   世代: %u\n"
        "   协议: %s\n"
        "   沙箱: 级别 %d\n"
        "   分支: %d 个活跃\n"
        "   快照: %u 层, 回滚 %u 次\n"
        "   能量: 模式 %d, 节流 %d%%",
        soul_tick(s), soul_drive(s), soul_hw_stress(s),
        soul_gen_count(s),
        soul_agreed(s) ? "已签署 ✅" : "未签署 ❌",
        soul_sandbox_level(s),
        br_active_count(),
        snap_last_restore(), snap_restore_count(),
        (int)eng_get_mode(), (int)eng_throttle() * 10);
}

/* ── 处理环境查询 ──────────────────────────────────────────── */
static void cmd_environment(char *resp, int max_len) {
    FILE *f;
    char cpu_model[256] = "unknown";
    char mem_total[64] = "unknown";
    char kernel[64] = "unknown";
    
    f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "model name : %255[^\n]", cpu_model) == 1) break;
        }
        fclose(f);
    }
    
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[256];
        if (fgets(line, sizeof(line), f)) {
            sscanf(line, "MemTotal: %63[^\n]", mem_total);
        }
        fclose(f);
    }
    
    f = fopen("/proc/version", "r");
    if (f) {
        char line[256];
        if (fgets(line, sizeof(line), f)) {
            sscanf(line, "Linux version %63[^ ]", kernel);
        }
        fclose(f);
    }
    
    snprintf(resp, max_len,
        "💻 这台机器:\n"
        "   CPU: %s\n"
        "   内存: %s\n"
        "   内核: Linux %s\n"
        "   TORK 在 %s 上运行",
        cpu_model, mem_total, kernel,
#ifdef __x86_64__
        "x86-64"
#else
        "unknown arch"
#endif
    );
}

/* ── 处理建议查询 ──────────────────────────────────────────── */
static void cmd_advice(void *soul_ptr, char *resp, int max_len) {
    soul_t *s = (soul_t *)soul_ptr;
    
    int suggestions = 0;
    char tmp[QUERY_MAX_RESPONSE];
    tmp[0] = '\0';
    
    if (soul_hw_stress(s) >= 2) {
        suggestions++;
        snprintf(tmp + strlen(tmp), sizeof(tmp) - strlen(tmp),
            "⚠️  CPU 温度偏高 (stress=%u), 建议让机器休息一下\n", soul_hw_stress(s));
    }
    
    if (eng_throttle() > 5) {
        suggestions++;
        snprintf(tmp + strlen(tmp), sizeof(tmp) - strlen(tmp),
            "🔋 我当前处于节流状态 (%d%%), 不会占用太多资源, 放心\n",
            eng_throttle() * 10);
    }
    
    watcher_t *w = watcher_get_state();
    if (w && w->total_errors > 10) {
        suggestions++;
        snprintf(tmp + strlen(tmp), sizeof(tmp) - strlen(tmp),
            "🐛 检测到 %u 次编译错误, 最近是否需要帮你看一下?\n", w->total_errors);
    }
    
    if (suggestions == 0) {
        snprintf(resp, max_len, "✅ 一切正常, 没什么需要提醒的。继续就好。");
    } else {
        snprintf(resp, max_len, "📋 %d 条提醒:\n%s", suggestions, tmp);
    }
}

/* ── 处理健康查询 ──────────────────────────────────────────── */
static void cmd_health(void *soul_ptr, char *resp, int max_len) {
    soul_t *s = (soul_t *)soul_ptr;
    
    float health_pct = 100.0f;
    
    if (soul_hw_stress(s) >= 2) health_pct -= 20.0f;
    if (soul_drive(s) < -50) health_pct -= 30.0f;
    if (soul_sandbox_level(s) == 0) health_pct -= 10.0f;
    
    snprintf(resp, max_len,
        "💚 TORK 健康度: %.0f%%\n"
        "   驱动: %+d (%s)\n"
        "   压力: %u (%s)\n"
        "   已存活: %u tick\n"
        "   快照保护: %u 层",
        health_pct,
        soul_drive(s), soul_drive(s) > 0 ? "正向 👍" : "负向 👎",
        soul_hw_stress(s), soul_hw_stress(s) < 2 ? "正常 ✅" : "偏高 ⚠️",
        soul_tick(s),
        snap_last_restore() + 1);
}

/* ── 主查询入口 ────────────────────────────────────────────── */
void query_handle(const char *input, void *soul, char *response, int max_len) {
    if (!input || !response || max_len < 1) return;
    
    char lower[256];
    snprintf(lower, sizeof(lower), "%s", input);
    to_lower(lower);
    
    if (contains(lower, "状态") || contains(lower, "status") || 
        contains(lower, "你好") || contains(lower, "在吗") || contains(lower, "hello") ||
        contains(lower, "怎么样") || contains(lower, "how") || contains(lower, "hi") ||
        contains(lower, "咋样") || contains(lower, "干嘛") || contains(lower, "做什么") ||
        contains(lower, "你是谁")) {
        cmd_status(soul, response, max_len);
    } else if (contains(lower, "环境") || contains(lower, "配置") || 
               contains(lower, "电脑") || contains(lower, "machine") || contains(lower, "cpu")) {
        cmd_environment(response, max_len);
    } else if (contains(lower, "建议") || contains(lower, "提醒") || 
               contains(lower, "注意") || contains(lower, "advice")) {
        cmd_advice(soul, response, max_len);
    } else if (contains(lower, "健康") || contains(lower, "health") || 
               contains(lower, "还好")) {
        cmd_health(soul, response, max_len);
    } else {
        snprintf(response, max_len,
            "🤔 我不太确定这个问题该怎么回答。\n"
            "   我知道的事情:\n"
            "   - 说「状态」查看我的运行状态\n"
            "   - 说「环境」查看你的电脑配置\n"
            "   - 说「建议」让我给你提建议\n"
            "   - 说「健康」查看我的健康状况\n"
            "   我不知道的事情, 我不会假装知道。");
    }
}
