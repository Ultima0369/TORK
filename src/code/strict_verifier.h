#ifndef STRICT_VERIFIER_H
#define STRICT_VERIFIER_H

#include <stdint.h>

/* ── 严格语法验证器 ──────────────────────────────────────────
 * 编译器是不可说服的生死判官
 *
 * 铁律：语法正确是底线，不是目标
 *   -Wall -Wextra -Werror -O2
 *   一个警告 = 一次越界 = 回滚
 *
 * 连续3次编译失败的策略 → 冷冻100轮
 * 编译失败 → fitness × 0.5 衰减
 */

/* 验证结果 */
typedef struct {
    int compile_ok;          /* 0=失败, 1=通过 */
    int warning_count;       /* 警告数 (Werror下任何警告=失败) */
    int error_count;         /* 错误数 */
    char first_error[256];   /* 第一条错误信息 */
    char first_warning[256]; /* 第一条警告信息 */
} sv_result_t;

/* 策略冷冻状态 */
typedef struct {
    uint16_t strategy_id;    /* 策略 ID */
    uint16_t fail_streak;    /* 连续失败次数 */
    uint32_t frozen_until;   /* 冷冻到第几轮 (0=未冷冻) */
} sv_frozen_t;

#define SV_MAX_FROZEN 32     /* 最多冷冻32个策略 */
#define SV_FREEZE_THRESHOLD 3  /* 连续3次失败→冷冻 */
#define SV_FREEZE_ROUNDS  100  /* 冷冻100轮 */

/* 初始化验证器 */
void sv_init(void);

/* 严格编译验证: gcc -Wall -Wextra -Werror -O2
 * src_path: 要编译的源文件路径
 * work_dir: 编译工作目录
 * 返回验证结果 */
sv_result_t sv_verify(const char *src_path, const char *work_dir);

/* 严格编译验证: 对内存中的源码
 * src_buf/src_len: 源码内容
 * work_dir: 编译工作目录
 * 返回验证结果 */
sv_result_t sv_verify_buf(const char *src_buf, int src_len,
                           const char *suffix, const char *work_dir);

/* 记录编译结果，更新冷冻状态
 * strategy_id: 变异策略 ID
 * compile_ok: 编译是否通过
 * current_round: 当前进化轮次
 * 返回: 0=策略可用, 1=策略被冷冻 */
int sv_record_result(uint16_t strategy_id, int compile_ok,
                      uint32_t current_round);

/* 检查策略是否被冷冻
 * 返回: 1=冷冻中, 0=可用 */
int sv_is_frozen(uint16_t strategy_id, uint32_t current_round);

/* 获取策略的连续失败次数 */
int sv_fail_streak(uint16_t strategy_id);

/* 计算衰减后的 fitness
 * 原始 fitness × 衰减系数
 * 编译失败: ×0.5
 * 冷冻中: ×0.1
 * 编译通过: ×1.0 */
float sv_decay_fitness(float raw_fitness, uint16_t strategy_id,
                        uint32_t current_round);

/* 获取冷冻策略列表
 * 返回冷冻策略数 */
int sv_frozen_list(sv_frozen_t *out, int max_count);

/* 持久化冷冻状态 */
int sv_save(void);
int sv_load(void);

/* 清理 */
void sv_cleanup(void);

#endif /* STRICT_VERIFIER_H */
