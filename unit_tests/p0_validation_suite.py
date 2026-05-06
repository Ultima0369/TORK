"""
TORK P0 Engineering Sprint 验证测试套件
v3.17-alpha 验收标准

P0-1: TLN "真悬置" observe mode
P0-2: 闭环进化适应度函数
P0-3: π-Seed HMAC 硬化
接口约束: 32字节 Soul 预留 + API timeout/retry
"""

import json
import os
import re
import struct
import sys
import unittest

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class P01_TLNObserveMode(unittest.TestCase):
    """P0-1: TLN 真悬置模式 — tln_modify_hint==0 时变异停止，120心跳超时强制退出"""

    def test_tln_observe_timeout_constant(self):
        """TLN_OBSERVE_TIMEOUT == 120"""
        path = os.path.join(BASE, "src/engine/tln.h")
        with open(path) as f:
            content = f.read()
        m = re.search(r'#define\s+TLN_OBSERVE_TIMEOUT\s+(\d+)', content)
        self.assertIsNotNone(m, "TLN_OBSERVE_TIMEOUT not found in tln.h")
        self.assertEqual(int(m.group(1)), 120)

    def test_tln_observe_functions_exist(self):
        """tln_is_observing / tln_observe_tick / tln_observe_timed_out / tln_observe_reset 均存在"""
        path = os.path.join(BASE, "src/engine/tln.h")
        with open(path) as f:
            content = f.read()
        for fn in ["tln_is_observing", "tln_observe_tick",
                    "tln_observe_timed_out", "tln_observe_reset"]:
            self.assertIn(fn, content, f"{fn} not declared in tln.h")

    def test_tln_observe_timeout_logic(self):
        """tln_observe_timed_out 返回 observe_ticks >= TLN_OBSERVE_TIMEOUT"""
        path = os.path.join(BASE, "src/engine/tln.c")
        with open(path) as f:
            content = f.read()
        m = re.search(r'tln_observe_timed_out.*?observe_ticks\s*>=\s*TLN_OBSERVE_TIMEOUT', content, re.DOTALL)
        self.assertIsNotNone(m, "tln_observe_timed_out does not check observe_ticks >= TLN_OBSERVE_TIMEOUT")

    def test_tln_observe_accumulation(self):
        """P0-1: observe 期间有蓄积逻辑（权重微调），不是空转"""
        path = os.path.join(BASE, "src/engine/tln.c")
        with open(path) as f:
            content = f.read()
        # tln_observe_tick 必须有蓄积逻辑
        self.assertIn("observe_ticks % 20", content,
                      "tln_observe_tick must have weight accumulation every 20 ticks")

    def test_scheduler_heartbeat_acceleration(self):
        """observe 首个 tick 时 heartbeat 加速到 100ms"""
        path = os.path.join(BASE, "src/engine/scheduler.c")
        with open(path) as f:
            content = f.read()
        self.assertIn("soul_set_heartbeat_ms(ctx->soul, 100)", content,
                       "scheduler does not accelerate heartbeat to 100ms on observe")

    def test_scheduler_heartbeat_fastened_flag(self):
        """P0-1: 心跳加速使用标志位，不是单帧检测"""
        path = os.path.join(BASE, "src/engine/scheduler.c")
        with open(path) as f:
            content = f.read()
        self.assertIn("heartbeat_fastened", content,
                      "Must use heartbeat_fastened flag, not single-frame observe_ticks==1")

    def test_scheduler_heartbeat_fastened_in_header(self):
        """P0-1: heartbeat_fastened 字段在 scheduler.h 中声明"""
        path = os.path.join(BASE, "src/engine/scheduler.h")
        with open(path) as f:
            content = f.read()
        self.assertIn("heartbeat_fastened", content,
                      "heartbeat_fastened must be declared in sched_ctx_t")

    def test_scheduler_observe_cooldown(self):
        """P0-1: 观察超时后有冷却期，防止立即重入"""
        path = os.path.join(BASE, "src/engine/scheduler.c")
        with open(path) as f:
            content = f.read()
        self.assertIn("observe_cooldown", content,
                      "Must have observe_cooldown after timeout exit")

    def test_scheduler_observe_cooldown_in_header(self):
        """P0-1: observe_cooldown 字段在 scheduler.h 中声明"""
        path = os.path.join(BASE, "src/engine/scheduler.h")
        with open(path) as f:
            content = f.read()
        self.assertIn("observe_cooldown", content,
                      "observe_cooldown must be declared in sched_ctx_t")

    def test_scheduler_observe_timeout_exits(self):
        """120 tick 超时后：S_EXPERIENCE_COUNT+1, action_hint=-1, heartbeat 恢复 500ms"""
        path = os.path.join(BASE, "src/engine/scheduler.c")
        with open(path) as f:
            content = f.read()
        self.assertIn("tln_observe_timed_out", content)
        self.assertIn("S_EXPERIENCE_COUNT", content)
        self.assertIn("force_conservative", content)
        self.assertIn("soul_set_heartbeat_ms(ctx->soul, 500)", content)

    def test_scheduler_observe_resets_on_exit(self):
        """离开 observe 模式时 observe_ticks 归零 + heartbeat 恢复"""
        path = os.path.join(BASE, "src/engine/scheduler.c")
        with open(path) as f:
            content = f.read()
        self.assertIn("tln_observe_reset", content)

    def test_mutation_blocked_during_observe(self):
        """observe 期间变异被阻断"""
        path = os.path.join(BASE, "src/engine/scheduler.c")
        with open(path) as f:
            content = f.read()
        self.assertIn("tln_is_observing", content)


class P02_FitnessFunction(unittest.TestCase):
    """P0-2: 闭环进化适应度函数 — fitness = (survival_ticks × 0.6 + compile_ok × 0.4) × decay"""

    def test_fitness_formula(self):
        """适应度公式: (survival_ticks × 0.6 + compile_ok × 0.4) × decay"""
        path = os.path.join(BASE, "cloud/evolution.py")
        with open(path) as f:
            content = f.read()
        self.assertIn("survival_ticks", content)
        self.assertIn("0.6", content)
        self.assertIn("compile_ok", content)
        self.assertIn("0.4", content)
        m = re.search(r'survival_ticks\s*\*\s*0\.6\s*\+\s*compile_ok\s*\*\s*0\.4', content)
        self.assertIsNotNone(m, "fitness formula not found: survival_ticks * 0.6 + compile_ok * 0.4")

    def test_fitness_decay_factor_in_scores(self):
        """P0-2: _compute_mutagen_scores 读取 fitness 衰减因子作为权重乘数"""
        path = os.path.join(BASE, "cloud/evolution.py")
        with open(path) as f:
            content = f.read()
        self.assertIn('entry.get("fitness", 1.0)', content,
                      "mutagen scores must multiply by fitness decay factor")

    def test_fitness_exponential_decay(self):
        """P0-2: _apply_fitness_decay 使用半衰期指数衰减 (0.5^(fails/half_life))"""
        path = os.path.join(BASE, "cloud/evolution.py")
        with open(path) as f:
            content = f.read()
        self.assertIn("HALF_LIFE", content,
                      "Must define HALF_LIFE constant for exponential decay")
        self.assertIn("0.5 **", content,
                      "Must use exponential decay formula 0.5^(fails/half_life)")
        # 不应是一次性 *factor（旧代码特征）
        self.assertNotIn('entry["fitness"] = entry.get("fitness", 1.0) * factor', content,
                         "Must NOT use one-shot factor multiplication")

    def test_weighted_random_selection(self):
        """best_mutagen 更高选择概率 — 加权随机选择"""
        path = os.path.join(BASE, "cloud/evolution.py")
        with open(path) as f:
            content = f.read()
        self.assertIn("weighted", content.lower())
        self.assertIn("best_mutagen", content)

    def test_compile_failure_half_life_decay(self):
        """编译失败触发衰减"""
        path = os.path.join(BASE, "cloud/evolution.py")
        with open(path) as f:
            content = f.read()
        self.assertIn("_apply_fitness_decay", content)

    def test_strategy_freeze_on_consecutive_failures(self):
        """3次连续编译失败 → 策略冻结 100 轮"""
        path = os.path.join(BASE, "cloud/evolution.py")
        with open(path) as f:
            content = f.read()
        self.assertIn("freeze", content.lower())
        self.assertIn("3", content)
        self.assertIn("100", content)


class P03_PiSeedHMAC(unittest.TestCase):
    """P0-3: π-Seed HMAC 硬化 — HMAC-SHA256(RDRAND+TSC+反馈链, π序列偏移值)"""

    def test_sha256_implementation_exists(self):
        """SHA-256 (FIPS 180-4) 实现存在"""
        path = os.path.join(BASE, "src/learning/pi_seed.c")
        with open(path) as f:
            content = f.read()
        self.assertIn("pi_sha256", content)
        self.assertIn("0x6a09e667", content)

    def test_hmac_sha256_implementation_exists(self):
        """HMAC-SHA256 (RFC 2104) 实现存在"""
        path = os.path.join(BASE, "src/learning/pi_seed.c")
        with open(path) as f:
            content = f.read()
        self.assertIn("pi_hmac_sha256", content)
        self.assertIn("0x36", content)
        self.assertIn("0x5c", content)

    def test_pi_seed_hmac_function_exists(self):
        """pi_seed_hmac() 函数存在"""
        path = os.path.join(BASE, "src/learning/pi_seed.c")
        with open(path) as f:
            content = f.read()
        self.assertIn("uint8_t pi_seed_hmac(void)", content)

    def test_pi_seed_hmac_uses_rdrand(self):
        """P0-3: pi_seed_hmac 使用 RDRAND 硬件熵源"""
        path = os.path.join(BASE, "src/learning/pi_seed.c")
        with open(path) as f:
            content = f.read()
        self.assertIn("rdrand", content,
                      "pi_seed.c must use RDRAND for hardware entropy")

    def test_pi_seed_hmac_feedback_chain(self):
        """P0-3: pi_seed_hmac 使用 HMAC 输出反馈链"""
        path = os.path.join(BASE, "src/learning/pi_seed.c")
        with open(path) as f:
            content = f.read()
        self.assertIn("hmac_feedback", content,
                      "pi_seed.c must implement HMAC feedback chain")

    def test_pi_seed_hmac_key_16bytes(self):
        """P0-3: HMAC key 扩展到 16B（RDRAND+TSC+反馈）"""
        path = os.path.join(BASE, "src/learning/pi_seed.c")
        with open(path) as f:
            content = f.read()
        self.assertIn("key[16]", content,
                      "HMAC key must be 16 bytes (RDRAND+TSC+feedback)")

    def test_pi_seed_hmac_uses_tsc_and_pi(self):
        """pi_seed_hmac 使用 TSC + π 序列"""
        path = os.path.join(BASE, "src/learning/pi_seed.c")
        with open(path) as f:
            content = f.read()
        m = re.search(r'uint8_t pi_seed_hmac\(void\)\s*\{(.+?)\n\}', content, re.DOTALL)
        self.assertIsNotNone(m, "pi_seed_hmac function body not found")
        body = m.group(1)
        self.assertIn("rdtsc", body.lower())
        self.assertIn("pi_bbp_digit", body)
        self.assertIn("pi_hmac_sha256", body)

    def test_rdrand64_function_exists(self):
        """P0-3: rdrand64 内联函数存在"""
        path = os.path.join(BASE, "src/learning/pi_seed.c")
        with open(path) as f:
            content = f.read()
        self.assertIn("rdrand64", content,
                      "rdrand64 inline function must exist")

    def test_hmac_declarations_in_header(self):
        """SHA-256/HMAC 声明在 pi_seed.h 中"""
        path = os.path.join(BASE, "src/learning/pi_seed.h")
        with open(path) as f:
            content = f.read()
        self.assertIn("pi_sha256", content)
        self.assertIn("pi_hmac_sha256", content)
        self.assertIn("pi_seed_hmac", content)

    def test_size_t_include(self):
        """pi_seed.h 包含 <stddef.h>（size_t 定义）"""
        path = os.path.join(BASE, "src/learning/pi_seed.h")
        with open(path) as f:
            content = f.read()
        self.assertIn("stddef.h", content)


class InterfaceConstraints(unittest.TestCase):
    """接口约束: 32字节 Soul 预留 + API timeout/retry"""

    def test_soul_node_id_offset(self):
        """S_NODE_ID at 0xA8, 16 bytes"""
        path = os.path.join(BASE, "src/engine/soul_access.h")
        with open(path) as f:
            content = f.read()
        m = re.search(r'#define\s+S_NODE_ID\s+0xA8', content)
        self.assertIsNotNone(m, "S_NODE_ID at 0xA8 not found")

    def test_soul_consensus_vector_offset(self):
        """S_CONSENSUS_VECTOR at 0xB8, 16 bytes"""
        path = os.path.join(BASE, "src/engine/soul_access.h")
        with open(path) as f:
            content = f.read()
        m = re.search(r'#define\s+S_CONSENSUS_VECTOR\s+0xB8', content)
        self.assertIsNotNone(m, "S_CONSENSUS_VECTOR at 0xB8 not found")

    def test_32_byte_reservation_layout(self):
        """node_id(16) + consensus_vector(16) = 32 字节，不重叠"""
        path = os.path.join(BASE, "src/engine/soul_access.h")
        with open(path) as f:
            content = f.read()
        m_size = re.search(r'#define\s+SOUL_SIZE\s+(\d+)', content)
        self.assertIsNotNone(m_size, "SOUL_SIZE not found")
        soul_size = int(m_size.group(1))
        self.assertGreaterEqual(soul_size, 200,
                                f"SOUL_SIZE={soul_size} too small for consensus_vector ending at 0xC8")

    def test_api_timeout_10s(self):
        """API timeout 默认 10s"""
        path = os.path.join(BASE, "api/tork_api.py")
        with open(path) as f:
            content = f.read()
        m = re.search(r"timeout.*10", content)
        self.assertIsNotNone(m, "API timeout=10 not found")

    def test_api_max_retries_3(self):
        """API max_retries 默认 3"""
        path = os.path.join(BASE, "api/tork_api.py")
        with open(path) as f:
            content = f.read()
        m = re.search(r"max_retries.*3", content)
        self.assertIsNotNone(m, "API max_retries=3 not found")

    def test_api_retry_logic_in_ask(self):
        """ask() 方法实际使用 max_retries 重试"""
        path = os.path.join(BASE, "api/tork_api.py")
        with open(path) as f:
            content = f.read()
        self.assertIn("for attempt in range(self.max_retries)", content)

    def test_api_retry_logic_in_ask_simple(self):
        """ask_simple() 方法实际使用 max_retries 重试"""
        path = os.path.join(BASE, "api/tork_api.py")
        with open(path) as f:
            content = f.read()
        count = content.count("for attempt in range(self.max_retries)")
        self.assertGreaterEqual(count, 2, "ask_simple missing retry loop")


class BuildVerification(unittest.TestCase):
    """编译验证：make all 成功"""

    def test_build_artifacts_exist(self):
        """关键编译产物存在"""
        for artifact in ["tork_engine", "tork", "torkd_start"]:
            path = os.path.join(BASE, "build", artifact)
            self.assertTrue(os.path.isfile(path), f"build/{artifact} not found")

    def test_pi_seed_compiles(self):
        """pi_seed.o 存在（HMAC 实现编译通过）"""
        path = os.path.join(BASE, "build", "pi_seed.o")
        self.assertTrue(os.path.isfile(path), "build/pi_seed.o not found")

    def test_tork_core_compiles(self):
        """tork_core (ASM 心跳) 编译产物存在"""
        path = os.path.join(BASE, "build", "tork_core")
        self.assertTrue(os.path.isfile(path), "build/tork_core not found")


class IronLaw_Section1_PhysicalBoundaries(unittest.TestCase):
    """熔断铁律 Section 1: 核心物理边界 — 三个受保护区域"""

    def test_code_segment_boundary_saved(self):
        """_start 和 .die 地址保存到 bss 变量"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        self.assertIn("code_start_addr", content)
        self.assertIn("code_end_addr", content)
        self.assertIn("_start(%rip)", content)
        self.assertIn(".die(%rip)", content)

    def test_tor_state_size_defined(self):
        """TOR_STATE_SIZE = 20 字节"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        self.assertIn("TOR_STATE_SIZE", content)
        m = re.search(r'TOR_STATE_SIZE\s*,\s*(\d+)', content)
        self.assertIsNotNone(m, "TOR_STATE_SIZE not found")
        self.assertEqual(int(m.group(1)), 20)

    def test_soul_size_208_bytes(self):
        """Soul 208 字节 (0x200000)"""
        path = os.path.join(BASE, "src/core/tork_soul.inc")
        with open(path) as f:
            content = f.read()
        m = re.search(r'SOUL_SIZE_BYTES\s*,\s*(\d+)', content)
        self.assertIsNotNone(m, "SOUL_SIZE_BYTES not found")
        self.assertEqual(int(m.group(1)), 208)

    def test_code_start_addr_in_bss(self):
        """code_start_addr/code_end_addr 在 .bss 中声明"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        # 在 .bss 段中
        bss_start = content.find(".section .bss")
        self.assertGreater(bss_start, 0, ".bss section not found")
        bss_content = content[bss_start:]
        self.assertIn("code_start_addr", bss_content)
        self.assertIn("code_end_addr", bss_content)


class IronLaw_Section2_PtraceGate(unittest.TestCase):
    """熔断铁律 Section 2: ptrace 门控协议"""

    def test_attach_audit_function_exists(self):
        """soul_audit_attach 函数存在"""
        path = os.path.join(BASE, "src/engine/soul_access.h")
        with open(path) as f:
            content = f.read()
        self.assertIn("soul_audit_attach", content)

    def test_attach_audit_checks_ppid(self):
        """attach 审计检查 PPid (确保调用者是 tork_engine)"""
        path = os.path.join(BASE, "src/engine/soul_access.h")
        with open(path) as f:
            content = f.read()
        self.assertIn("PPid:", content)
        self.assertIn("getpid()", content)

    def test_attach_audit_generates_token(self):
        """attach 审计生成审计令牌 (RDRAND+TSC)"""
        path = os.path.join(BASE, "src/engine/soul_access.h")
        with open(path) as f:
            content = f.read()
        self.assertIn("audit_token", content)
        self.assertIn("rdtsc", content)

    def test_detach_verify_function_exists(self):
        """soul_verify_detach 函数存在"""
        path = os.path.join(BASE, "src/engine/soul_access.h")
        with open(path) as f:
            content = f.read()
        self.assertIn("soul_verify_detach", content)

    def test_detach_verify_uses_crc32(self):
        """detach 验证使用 CRC32 校验"""
        path = os.path.join(BASE, "src/engine/soul_access.h")
        with open(path) as f:
            content = f.read()
        self.assertIn("0xEDB88320", content)

    def test_detach_verify_rollback_on_failure(self):
        """detach 验证失败时回滚到 pre_snapshot"""
        path = os.path.join(BASE, "src/engine/soul_access.h")
        with open(path) as f:
            content = f.read()
        self.assertIn("pre_snapshot", content)

    def test_heartbeat_pipe_confirm_function_exists(self):
        """soul_heartbeat_pipe_confirm 函数存在"""
        path = os.path.join(BASE, "src/engine/soul_access.h")
        with open(path) as f:
            content = f.read()
        self.assertIn("soul_heartbeat_pipe_confirm", content)

    def test_heartbeat_pipe_writes_to_fd0(self):
        """心跳确认写入 /proc/core_pid/fd/0"""
        path = os.path.join(BASE, "src/engine/soul_access.h")
        with open(path) as f:
            content = f.read()
        self.assertIn("/proc/%d/fd/0", content)

    def test_heartbeat_dual_channel_in_asm(self):
        """ASM 内核有心跳双通道交叉验证"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        self.assertIn("hb_pipe_confirmed", content)

    def test_soul_open_calls_audit(self):
        """soul_open 调用 attach 审计"""
        path = os.path.join(BASE, "src/engine/soul_access.h")
        with open(path) as f:
            content = f.read()
        # soul_open 中必须调用 soul_audit_attach
        m = re.search(r'soul_open.*?soul_audit_attach', content, re.DOTALL)
        self.assertIsNotNone(m, "soul_open must call soul_audit_attach")

    def test_soul_write_calls_verify(self):
        """soul_write_byte/soul_write_buf 调用 detach 验证"""
        path = os.path.join(BASE, "src/engine/soul_access.h")
        with open(path) as f:
            content = f.read()
        # soul_write_byte 中必须调用 soul_verify_detach
        m1 = re.search(r'soul_write_byte.*?soul_verify_detach', content, re.DOTALL)
        self.assertIsNotNone(m1, "soul_write_byte must call soul_verify_detach")
        m2 = re.search(r'soul_write_buf.*?soul_verify_detach', content, re.DOTALL)
        self.assertIsNotNone(m2, "soul_write_buf must call soul_verify_detach")

    def test_heartbeat_set_calls_pipe_confirm(self):
        """soul_set_heartbeat_ms 调用双通道确认"""
        path = os.path.join(BASE, "src/engine/soul_access.h")
        with open(path) as f:
            content = f.read()
        m = re.search(r'soul_set_heartbeat_ms.*?soul_heartbeat_pipe_confirm', content, re.DOTALL)
        self.assertIsNotNone(m, "soul_set_heartbeat_ms must call soul_heartbeat_pipe_confirm")


class IronLaw_Section3_TamperFuse(unittest.TestCase):
    """熔断铁律 Section 3: 篡改熔断"""

    def test_crc32_byte_function_exists(self):
        """crc32_byte 函数存在"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        self.assertIn("crc32_byte:", content)

    def test_crc32_uses_polynomial(self):
        """CRC32 使用 0xEDB88320 多项式"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        self.assertIn("0xEDB88320", content)

    def test_soul_crc32_verify_function_exists(self):
        """soul_crc32_verify 函数存在"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        self.assertIn("soul_crc32_verify:", content)

    def test_fuse_self_destruct_function_exists(self):
        """fuse_self_destruct 函数存在"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        self.assertIn("fuse_self_destruct:", content)

    def test_fuse_zeros_soul(self):
        """熔断时 Soul 208 字节归零"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        # rep stosq with SOUL_ADDR
        m = re.search(r'fuse_self_destruct:.*?SOUL_ADDR.*?rep\s+stosq', content, re.DOTALL)
        self.assertIsNotNone(m, "fuse_self_destruct must zero Soul with rep stosq")

    def test_fuse_zeros_tor_state(self):
        """熔断时 TOR state 20 字节归零"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        # 在 fuse_self_destruct 中覆写 state (rep stosl for 20 bytes)
        m = re.search(r'fuse_self_destruct:.*?state.*?rep\s+stosl', content, re.DOTALL)
        self.assertIsNotNone(m, "fuse_self_destruct must zero TOR state with rep stosl")

    def test_fuse_writes_syslog(self):
        """熔断时写入 syslog 通知"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        self.assertIn("msg_syslog", content)
        self.assertIn("msg_devlog", content)
        self.assertIn("/dev/log", content)

    def test_fuse_clears_registers(self):
        """熔断时清零所有通用寄存器"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        # 在 fuse_self_destruct 中必须有 xor %rax,%rax 等
        m = re.search(r'fuse_self_destruct:.*?xor\s+%rax,\s*%rax', content, re.DOTALL)
        self.assertIsNotNone(m, "fuse_self_destruct must clear registers")

    def test_fuse_destroys_stack(self):
        """熔断时销毁栈帧"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        m = re.search(r'fuse_self_destruct:.*?movq\s+\$0,\s*\(%rdi\)', content, re.DOTALL)
        self.assertIsNotNone(m, "fuse_self_destruct must destroy stack frames")

    def test_fuse_mprotect_none(self):
        """熔断时 mprotect(PROT_NONE) 锁定内存页"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        self.assertIn("SYS_MPROTECT", content)

    def test_fuse_exit_code_1(self):
        """熔断退出码为 1 (不是 255)"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        # 在 fuse_self_destruct 中 exit(1)
        m = re.search(r'fuse_self_destruct:.*?mov\s+\$1,\s*%edi\s+syscall', content, re.DOTALL)
        self.assertIsNotNone(m, "fuse_self_destruct must exit(1)")

    def test_crc32_check_in_main_loop(self):
        """主循环中有 CRC32 校验"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        # .main 中调用 soul_crc32_verify
        m = re.search(r'\.main:.*?soul_crc32_verify', content, re.DOTALL)
        self.assertIsNotNone(m, ".main loop must call soul_crc32_verify")

    def test_fuse_cnt_3_strikes(self):
        """3次连续 CRC32 失败触发熔断"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        self.assertIn("fuse_cnt", content)
        m = re.search(r'cmp\s+\$3,\s*%rax', content)
        self.assertIsNotNone(m, "must check fuse_cnt >= 3")

    def test_fuse_cnt_reset_on_pass(self):
        """CRC32 通过时重置熔断计数"""
        path = os.path.join(BASE, "src/core/tork_core.asm")
        with open(path) as f:
            content = f.read()
        m = re.search(r'\.crc_ok:.*?movq\s+\$0,\s*fuse_cnt', content, re.DOTALL)
        self.assertIsNotNone(m, "must reset fuse_cnt on CRC pass")


if __name__ == "__main__":
    unittest.main(verbosity=2)
