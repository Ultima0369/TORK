#!/usr/bin/env python3
"""
TORK 策略生成器 v1.0 — DeepSeek 驱动 + 失败分析反馈

职责：
  1. 根据当前项目状态（代码行数/本能值/适应度历史/失败模式）向 DeepSeek 请求策略
  2. 解析 DeepSeek 返回的 JSON 格式策略描述
  3. 将失败模式编译为"避免重复"的约束条件
  4. 输出统一格式的策略字典，供 evolution.py 消费

输出策略格式:
  {
    "mutagen": "生成的唯一标识符",
    "description": "人类可读描述",
    "target_file": "src/engine/xxx.c",
    "action_type": "mod_param|inject_marker|mcts_action|code_modifier",
    "action_params": { ... },
    "confidence": 0.0-1.0,           # DeepSeek 自评置信度
    "deepseek_rationale": "..."      # LLM 的解释
  }
"""

from __future__ import annotations

import json
import os
import re
import sys
import time
from typing import Any

BASE: str = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(BASE, 'api'))


# ── 已知的可调制参数景观 (instinct.c + calibrator.h) ──────────
# 这些是 evolution.py 知道如何安全调整的参数
KNOWN_PARAMS: dict[str, dict[str, Any]] = {
    "fear_weight": {
        "file": "src/instinct/instinct.c",
        "type": "struct_field",
        "struct": "fallback_params",
        "min": 10, "max": 500,
        "default": 100,
        "description": "恐惧灵敏度 (×0.01 乘到 fear 基数)",
    },
    "desire_weight": {
        "file": "src/instinct/instinct.c",
        "type": "struct_field",
        "struct": "fallback_params",
        "min": 10, "max": 500,
        "default": 100,
        "description": "欲望灵敏度",
    },
    "curiosity_weight": {
        "file": "src/instinct/instinct.c",
        "type": "struct_field",
        "struct": "fallback_params",
        "min": 10, "max": 500,
        "default": 100,
        "description": "好奇心灵敏度",
    },
    "conservative_cycle": {
        "file": "src/instinct/instinct.c",
        "type": "struct_field",
        "struct": "fallback_params",
        "min": 5, "max": 200,
        "default": 30,
        "description": "保守模式下 tick 间隔基数",
    },
    "aggressive_cycle": {
        "file": "src/instinct/instinct.c",
        "type": "struct_field",
        "struct": "fallback_params",
        "min": 10, "max": 300,
        "default": 60,
        "description": "激进模式 tick 间隔基数",
    },
    "nop_cycle": {
        "file": "src/instinct/instinct.c",
        "type": "struct_field",
        "struct": "fallback_params",
        "min": 20, "max": 500,
        "default": 90,
        "description": "NOP清理模式 tick 间隔基数",
    },
    # 本能#define常量（行9-51的常量宏）
    "FEAR_HIGH": {
        "file": "src/instinct/instinct.c",
        "type": "define",
        "min": 0.1, "max": 5.0,
        "default": 1.0,
        "description": "高压力恐惧基数",
    },
    "FEAR_MED": {
        "file": "src/instinct/instinct.c",
        "type": "define",
        "min": 0.1, "max": 3.0,
        "default": 0.6,
        "description": "中压力恐惧基数",
    },
    "DESIRE_MOD": {
        "file": "src/instinct/instinct.c",
        "type": "define",
        "min": 0.1, "max": 3.0,
        "default": 0.8,
        "description": "成功修改欲望基数",
    },
    "CURI_RATIO": {
        "file": "src/instinct/instinct.c",
        "type": "define",
        "min": 0.05, "max": 2.0,
        "default": 0.5,
        "description": "控制流比率好奇系数",
    },
}

# ── 已知的 TORK_EVOLVE 标记位置 ─────────────────────────────
KNOWN_MARKERS: dict[str, dict[str, Any]] = {
    "instinct_return_before": {
        "file": "src/instinct/instinct.c",
        "protocols": ["INSERT_BEFORE"],
        "description": "instinct_evaluate 的 return 语句前",
    },
    "engine_include_insert": {
        "file": "src/engine/tork_engine.c",
        "protocols": ["INSERT_AFTER"],
        "description": "tork_engine.c 的 #include 区域",
    },
    "engine_rounds_insert": {
        "file": "src/engine/tork_engine.c",
        "protocols": ["INSERT_BEFORE"],
        "description": "tork_engine.c 主循环轮次计数器",
    },
    "sandbox_devtools_insert": {
        "file": "src/sandbox/sandbox.c",
        "protocols": ["INSERT_AFTER"],
        "description": "sandbox.c 开发工具白名单",
    },
}

# ── 已知的 TORK_EVOLVE 标记位置 ─────────────────────────────
# 从 mcts.h 提取的 11 个动作可以直接通过 TorkAPI 发送
MCTS_ACTIONS: dict[int, str] = {
    0: "ADJUST_FEAR",
    1: "ADJUST_CURIOSITY",
    2: "ADJUST_HEARTBEAT",
    3: "TRY_MODIFY",
    4: "TRY_OPTIMIZE",
    5: "ENTER_IDLE",
    6: "CALL_CLOUD",
    7: "MOD_REPLACE_OP",
    8: "MOD_DEL_DEAD",
    9: "MOD_DEL_NOP",
    10: "MOD_SWAP_REGS",
}


# ── 失败模式知识库 ──────────────────────────────────────────
# 当变异失败时，分析失败模式并记录约束
class FailurePattern:
    """记录一种已知的失败模式及避免策略"""
    
    def __init__(self, pattern_id: str, description: str,
                 avoid_action: str, avoid_params: dict[str, Any] | None = None,
                 cooldown_generations: int = 50) -> None:
        self.pattern_id: str = pattern_id
        self.description: str = description
        self.avoid_action: str = avoid_action        # 应避免的动作类型
        self.avoid_params: dict[str, Any] | None = avoid_params
        self.last_seen_gen: int = -1
        self.count: int = 0
        self.cooldown: int = cooldown_generations
    
    def matches(self, action_type: str, action_params: dict[str, Any]) -> bool:
        if action_type != self.avoid_action:
            return False
        if self.avoid_params:
            for k, v in self.avoid_params.items():
                if v == "*" and k in action_params:
                    return True
                if action_params.get(k) == v:
                    return True
            return False
        return True


# ── DeepSeek 策略生成器 ──────────────────────────────────────

class StrategyGenerator:
    """从 DeepSeek 生成变异的策略，支持失败反馈"""
    
    def __init__(self, api: Any | None = None) -> None:
        self.api: Any | None = api
        self.failure_patterns: list[FailurePattern] = []
        self._init_failure_patterns()
    
    def _init_failure_patterns(self) -> None:
        """初始化已知失败模式"""
        self.failure_patterns = [
            FailurePattern(
                "param_below_min",
                "参数调整低于最小值导致编译警告",
                "mod_param",
                {"param": "*", "delta": "negative"},
                cooldown_generations=30,
            ),
            FailurePattern(
                "marker_already_injected",
                "同一 TORK_EVOLVE 标记被重复注入",
                "inject_marker",
                {"marker": "*"},
                cooldown_generations=100,
            ),
            FailurePattern(
                "struct_not_found",
                "结构体字段未找到（文件可能已被修改）",
                "mod_param",
                {"method": "struct_field"},
                cooldown_generations=50,
            ),
        ]
    
    def record_failure(self, action_type: str, action_params: dict[str, Any],
                       error_msg: str, generation: int) -> str | None:
        """记录一次失败，返回匹配的 failure_pattern_id 或 None"""
        # 检查是否匹配已知模式
        for fp in self.failure_patterns:
            if fp.matches(action_type, action_params):
                fp.last_seen_gen = generation
                fp.count += 1
                return fp.pattern_id
        
        # 未知失败模式 — 提取特征
        if "compile" in error_msg.lower() or "error:" in error_msg.lower():
            # 提取第一个错误行作为新模式的特征
            lines: list[str] = error_msg.strip().split("\n")
            first_err: str = lines[0][:100] if lines else error_msg[:100]
            pat_id: str = f"unknown_compile_{len(self.failure_patterns)}"
            self.failure_patterns.append(FailurePattern(
                pat_id, f"未知编译错误: {first_err}",
                action_type, action_params,
                cooldown_generations=30,
            ))
            return pat_id
        
        return None
    
    def get_constraints_for_prompt(self) -> str:
        """生成约束描述文本，嵌入 DeepSeek prompt"""
        if not self.failure_patterns:
            return "无已知失败模式。"
        
        active: list[str] = []
        for fp in self.failure_patterns:
            if fp.count > 0:
                active.append(
                    f"  - {fp.description} (已发生{fp.count}次, "
                    f"冷却{fp.cooldown}代)"
                )
        
        return "\n".join(active) if active else "无活跃失败模式。"
    
    def generate(self, assessment: dict[str, Any],
                 recent_mutations: list[dict[str, Any]]) -> list[dict[str, Any]]:
        """
        主入口：生成一组候选策略。
        
        返回策略列表（可能为空）。
        """
        if not self.api:
            # fallback: 返回一个"无 API 可用"的兜底策略
            return self._fallback_strategies(assessment)
        
        strategies: list[dict[str, Any]] = []
        
        # 第一源：DeepSeek 生成
        ds_strategies: list[dict[str, Any]] = self._ask_deepseek(
            assessment, recent_mutations
        )
        strategies.extend(ds_strategies)
        
        # 第二源：如果 DeepSeek 没返回有效策略，用 fallback
        if not strategies:
            strategies = self._fallback_strategies(assessment)
        
        # 第三源：标记一些可靠的已知可调制参数
        known_strategies: list[dict[str, Any]] = self._generate_known_param_tweaks(
            assessment, recent_mutations
        )
        strategies.extend(known_strategies)
        
        return strategies
    
    def _ask_deepseek(self, assessment: dict[str, Any],
                      recent_mutations: list[dict[str, Any]]) -> list[dict[str, Any]]:
        """向 DeepSeek 请求策略，解析 JSON 响应"""
        if not self.api:
            return []
        
        # 构建当前可用的参数和标记列表（让 LLM 知道能调什么）
        param_names: list[str] = sorted(KNOWN_PARAMS.keys())
        marker_names: list[str] = sorted(KNOWN_MARKERS.keys())
        mcts_list: list[str] = [f"{k}={v}" for k, v in MCTS_ACTIONS.items()]
        
        recent_text: str = "无"
        if recent_mutations:
            recent_text = "\n".join([
                f"  Gen {m.get('generation','?')}: "
                f"{m.get('description','?')} "
                f"[{m.get('result','?')}] "
                f"(mutagen={m.get('mutagen','?')})"
                for m in recent_mutations[-5:]
            ])
        
        constraints: str = self.get_constraints_for_prompt()
        
        prompt: str = f"""你是 TORK 自进化引擎的云端导师。当前状态：

=== 项目状态 ===
Generation: {assessment.get('generation', 0)}
代码量: {assessment.get('total_lines', 0)} 行 / {assessment.get('total_files', 0)} 文件
总变异: {assessment.get('total_mutations', 0)} 次
成功率: {assessment.get('success_rate', 0) * 100:.0f}%

=== 最近变异 ===
{recent_text}

=== 失败约束 ===
{constraints}

=== 可调参数 ===
{', '.join(param_names)}

=== 可用 TORK_EVOLVE 标记 ===
{', '.join(marker_names)}

=== MCTS 动作空间 ===
{', '.join(mcts_list)}

=== 指令 ===
请分析当前状态并返回 **1-3 个** JSON 格式的策略建议。
每个策略必须严格使用以下格式之一：

1. 调参策略（mod_param）：
   {{"mutagen": "唯一英文标识符", "description": "简短中文描述", "target_file": "src/instinct/instinct.c", "action_type": "mod_param", "action_params": {{"param": "参数名", "delta": 整数增量, "method": "struct_field"}}, "confidence": 0.0-1.0, "rationale": "为什么选这个参数和增量"}}

2. 标记注入策略（inject_marker）：
   {{"mutagen": "唯一英文标识符", "description": "简短中文描述", "target_file": "文件路径", "action_type": "inject_marker", "action_params": {{"marker": "标记名", "code": "/* evo_injected */ 要插入的C代码(一行)", "protocol": "INSERT_BEFORE"}}, "confidence": 0.0-1.0, "rationale": "为什么在这里注入这段代码"}}

3. MCTS 桥接策略（mcts_action）：
   {{"mutagen": "唯一英文标识符", "description": "简短中文描述", "target_file": "src/learning/mcts.h", "action_type": "mcts_action", "action_params": {{"mcts_type": 动作编号(0-10), "param": 参数(-128到127)}}, "confidence": 0.0-1.0, "rationale": "为什么选这个MCTS动作"}}

请只输出 JSON 数组，不要额外的解释文字。每个策略的 rationale 字段写中文分析。"""

        try:
            reply: str = self.api.ask_simple(prompt, temperature=0.4)
            strategies: list[dict[str, Any]] = self._parse_deepseek_reply(reply)
            return strategies
        except Exception:
            return []
    
    def _parse_deepseek_reply(self, reply: str) -> list[dict[str, Any]]:
        """解析 DeepSeek 返回的 JSON"""
        # 提取 JSON 数组
        json_match: re.Match[str] | None = re.search(
            r'\[\s*\{.*\}\s*\]', reply, re.DOTALL
        )
        if not json_match:
            return []
        
        try:
            strategies: list[dict[str, Any]] = json.loads(json_match.group(0))
        except (json.JSONDecodeError, TypeError):
            return []
        
        # 验证和清洗
        valid: list[dict[str, Any]] = []
        for s in strategies:
            if self._validate_strategy(s):
                valid.append(s)
        
        return valid
    
    def _validate_strategy(self, s: dict[str, Any]) -> bool:
        """验证单个策略的字段完整性"""
        required: list[str] = [
            "mutagen", "description", "target_file",
            "action_type", "action_params"
        ]
        for r in required:
            if r not in s:
                return False
        
        valid_types: list[str] = ["mod_param", "inject_marker", "mcts_action"]
        if s["action_type"] not in valid_types:
            return False
        
        # 类型特定验证
        if s["action_type"] == "mod_param":
            if "param" not in s.get("action_params", {}):
                return False
            if s["action_params"]["param"] not in KNOWN_PARAMS:
                return False
            # 使用默认文件路径
            s["target_file"] = KNOWN_PARAMS[s["action_params"]["param"]]["file"]
        
        if s["action_type"] == "inject_marker":
            if "marker" not in s.get("action_params", {}):
                return False
            if s["action_params"]["marker"] not in KNOWN_MARKERS:
                return False
            if "code" not in s.get("action_params", {}):
                return False
        
        if s["action_type"] == "mcts_action":
            mcts_type: int = s.get("action_params", {}).get("mcts_type", -1)
            if mcts_type not in MCTS_ACTIONS:
                return False
            param: int = s.get("action_params", {}).get("param", 0)
            if not (-128 <= param <= 127):
                return False
        
        return True
    
    def _fallback_strategies(self, assessment: dict[str, Any]) -> list[dict[str, Any]]:
        """无 DeepSeek 时的兜底策略"""
        gen: int = assessment.get("generation", 0)
        strategies: list[dict[str, Any]] = []
        
        # 轮换不同的参数
        param_cycle: list[str] = [
            "curiosity_weight", "fear_weight", "desire_weight",
            "conservative_cycle", "FEAR_HIGH", "DESIRE_MOD",
        ]
        param: str = param_cycle[gen % len(param_cycle)]
        delta: int = 15 if gen % 3 != 2 else -10  # 有时减一点
        
        strategies.append({
            "mutagen": f"fallback_param_{param}_{gen}",
            "description": f"兜底调参: {param} {'+' if delta > 0 else ''}{delta}",
            "target_file": KNOWN_PARAMS[param]["file"],
            "action_type": "mod_param",
            "action_params": {
                "param": param,
                "delta": delta,
                "method": KNOWN_PARAMS[param]["type"],
            },
            "confidence": 0.3,
            "rationale": f"无API可用时的轮换调参，第{gen}代选中{param}",
        })
        
        return strategies
    
    def _generate_known_param_tweaks(self, assessment: dict[str, Any],
                                      recent_mutations: list[dict[str, Any]]) -> list[dict[str, Any]]:
        """从已知参数中生成一些可靠的微调策略"""
        strategies: list[dict[str, Any]] = []
        gen: int = assessment.get("generation", 0)
        
        # 检查最近 10 次变异中有哪些参数被调过
        recently_tweaked: set[str] = set()
        for m in recent_mutations[-10:]:
            act_type: str = m.get("action_type", "")
            if act_type == "mod_param":
                p: str = m.get("action_params", {}).get("param", "")
                if p:
                    recently_tweaked.add(p)
        
        # 对最近未调的参数生成微调策略
        untweaked: list[str] = [
            p for p in KNOWN_PARAMS if p not in recently_tweaked
        ]
        
        # 取前 2 个未调参数
        for param in untweaked[:2]:
            delta: int = 10 if gen % 2 == 0 else -5
            strategies.append({
                "mutagen": f"known_{param}_{gen}",
                "description": f"已知参数微调: {param} {'+' if delta > 0 else ''}{delta}",
                "target_file": KNOWN_PARAMS[param]["file"],
                "action_type": "mod_param",
                "action_params": {
                    "param": param,
                    "delta": delta,
                    "method": KNOWN_PARAMS[param]["type"],
                },
                "confidence": 0.5,
                "rationale": f"参数{param}最近未被调整，进行探索性微调",
            })
        
        return strategies


# ── 失败分析器 ──────────────────────────────────────────────

class FailureAnalyzer:
    """分析变异失败的原因，生成约束反馈"""
    
    def __init__(self) -> None:
        self.failure_history: list[dict[str, Any]] = []
    
    def analyze(self, mutation_record: dict[str, Any],
                error_output: str) -> dict[str, Any]:
        """分析一次失败，返回分析结果"""
        result: dict[str, Any] = {
            "mutagen": mutation_record.get("mutagen", "unknown"),
            "generation": mutation_record.get("generation", -1),
            "result": mutation_record.get("result", "unknown"),
            "patterns": [],
            "suggestion": None,
        }
        
        error_lower: str = error_output.lower()
        
        # 检测已知模式
        if "compile" in result.get("result", ""):
            if "error:" in error_lower:
                result["patterns"].append("compile_error")
                # 提取第一个错误行用于智能分析
                lines: list[str] = error_output.split("\n")
                for line in lines:
                    if "error:" in line.lower():
                        result["suggestion"] = (
                            f"编译错误: {line.strip()[:120]}. "
                            "建议：检查代码注入的语法正确性，或使用不同的策略类型。"
                        )
                        break
        
        if "apply_failed" in result.get("result", ""):
            result["patterns"].append("apply_failed")
            result["suggestion"] = (
                "策略应用失败：标记可能已被注入，或文件结构已变更。"
                "建议换一个标记或参数。"
            )
        
        if "timeout" in result.get("result", ""):
            result["patterns"].append("compile_timeout")
            result["suggestion"] = (
                "编译超时：注入的代码可能触发了无限循环展开，"
                "或 Makefile 循环依赖。建议减少注入代码量。"
            )
        
        self.failure_history.append(result)
        return result


# ── 快速测试 ────────────────────────────────────────────────

if __name__ == "__main__":
    # 模拟测试
    gen: StrategyGenerator = StrategyGenerator(api=None)
    strategies: list[dict[str, Any]] = gen.generate(
        {"generation": 42, "total_lines": 18000, "total_files": 21,
         "total_mutations": 50, "success_rate": 0.6},
        []
    )
    print(f"生成 {len(strategies)} 个策略:")
    for s in strategies:
        print(f"  [{s['action_type']}] {s['description']} "
              f"(conf={s['confidence']})")
