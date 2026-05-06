#!/usr/bin/env python3
"""
TORK 代码桥接器 v1.0 — Python ↔ C 变异执行层

职责：
  1. 将 evolution.py 的策略转化为实际的 C 源码修改
  2. 桥接到 code_modifier.c 的 5 个实现动作（通过直接调用或生成补丁）
  3. 桥接到 mcts.h 的 11 个动作空间（通过 TorkAPI 发送指令）
  4. 执行参数调制（struct field / #define 修改）
  5. TORK_EVOLVE 标记注入（同原 inject_at_marker，但支持更多协议）
  6. 备份与回滚（文件级 + git 级）

支持的 action_type:
  - mod_param:     修改 C 源文件中的参数值
  - inject_marker: 在 TORK_EVOLVE 标记处注入代码
  - mcts_action:   通过 TorkAPI 向 C 引擎发送 MCTS 指令
  - code_modifier: 直接调用 code_modifier.c 的 ASM 变异函数
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from typing import Any

BASE: str = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# ── 参数调制引擎 ─────────────────────────────────────────────

class ParamModulator:
    """修改 C 源码中的参数值（struct field / #define）"""
    
    # 已知的 instinct.c 参数定义模式
    STRUCT_PATTERN: re.Pattern[str] = re.compile(
        r'(\.\s*{param_name}\s*=\s*)(\d+)'
    )
    DEFINE_PATTERN: re.Pattern[str] = re.compile(
        r'(#define\s+{param_name}\s+)([\d.]+)'
    )
    
    def __init__(self) -> None:
        self.backup_path: str | None = None
    
    def modulate(self, filepath: str, param_name: str, delta: int,
                 method: str = "struct_field") -> bool:
        """
        修改参数值。
        
        Args:
            filepath: C 源文件路径
            param_name: 参数名
            delta: 增量（可正可负）
            method: "struct_field" 或 "define"
        
        Returns:
            True 如果修改成功
        """
        full_path: str = os.path.join(BASE, filepath.lstrip("./"))
        if not os.path.exists(full_path):
            print(f"  [BRIDGE] 文件不存在: {full_path}")
            return False
        
        with open(full_path, 'r') as f:
            content: str = f.read()
        
        if method == "struct_field":
            pattern_str: str = rf'(\.\s*{re.escape(param_name)}\s*=\s*)(\d+)'
            match: re.Match[str] | None = re.search(pattern_str, content)
            if not match:
                print(f"  [BRIDGE] 结构体字段 {param_name} 未找到")
                return False
            
            old_val: int = int(match.group(2))
            new_val: int = old_val + delta
            
            # 边界检查
            from cloud.strategy_generator import KNOWN_PARAMS
            param_info: dict[str, Any] = KNOWN_PARAMS.get(param_name, {})
            if param_info:
                new_val = max(param_info.get("min", 0), 
                             min(param_info.get("max", 1000), new_val))
            
            old_str: str = match.group(0)
            new_str: str = match.group(1) + str(new_val)
            content = content.replace(old_str, new_str, 1)
            
        elif method == "define":
            pattern_str = rf'(#define\s+{re.escape(param_name)}\s+)([\d.]+)'
            match = re.search(pattern_str, content)
            if not match:
                print(f"  [BRIDGE] #define {param_name} 未找到")
                return False
            
            old_val_str: str = match.group(2)
            if '.' in old_val_str:
                old_val = float(old_val_str)
                new_val = old_val + delta
            else:
                old_val = int(old_val_str)
                new_val = old_val + delta
            
            old_str = match.group(0)
            new_str = match.group(1) + str(new_val)
            content = content.replace(old_str, new_str, 1)
        else:
            print(f"  [BRIDGE] 未知方法: {method}")
            return False
        
        with open(full_path, 'w') as f:
            f.write(content)
        
        print(f"  [BRIDGE] {param_name}: {old_val} → {new_val} ({delta:+d})")
        return True
    
    def parse_delta_string(self, delta_str: str) -> int:
        """解析增量字符串如 '+15', '-5', '10'"""
        delta_str = delta_str.strip()
        if delta_str.startswith('+'):
            return int(delta_str[1:])
        elif delta_str.startswith('-'):
            return int(delta_str)
        else:
            return int(delta_str)


# ── 标记注入引擎 ─────────────────────────────────────────────

class MarkerInjector:
    """在 TORK_EVOLVE 标记处注入代码"""
    
    GUARD_SIG: str = '/* evo_injected */'
    
    def __init__(self) -> None:
        self.backup_path: str | None = None
    
    def inject(self, filepath: str, marker_name: str, code: str,
               protocol: str = "INSERT_BEFORE") -> bool:
        """
        在标记处注入代码。
        
        Args:
            filepath: 目标文件路径
            marker_name: TORK_EVOLVE 标记名（不含 'TORK_EVOLVE: ' 前缀）
            code: 要注入的代码行（自动添加 // evo_injected 注释）
            protocol: INSERT_BEFORE | INSERT_AFTER | REPLACE_LINE
        
        Returns:
            True 如果注入成功
        """
        full_path: str = os.path.join(BASE, filepath.lstrip("./"))
        if not os.path.exists(full_path):
            print(f"  [BRIDGE] 文件不存在: {full_path}")
            return False
        
        with open(full_path, 'r') as f:
            lines: list[str] = f.readlines()
        
        marker_line: int | None = None
        for i, line in enumerate(lines):
            if f'TORK_EVOLVE: {marker_name}' in line:
                marker_line = i
                break
        
        if marker_line is None:
            print(f"  [BRIDGE] 标记 '{marker_name}' 未找到")
            return False
        
        # 双重注入防护
        if protocol in ('INSERT_BEFORE', 'INSERT_AFTER'):
            check_idx: int = marker_line - 1 if protocol == 'INSERT_BEFORE' else marker_line + 1
            if 0 <= check_idx < len(lines) and self.GUARD_SIG in lines[check_idx]:
                print(f"  [BRIDGE] 标记 '{marker_name}' 已有注入，跳过")
                return False
        
        # 确保代码行有 guard 签名
        if self.GUARD_SIG not in code:
            code = code.rstrip() + f'  {self.GUARD_SIG}'
        
        if protocol == 'INSERT_BEFORE':
            lines.insert(marker_line, code + '\n')
        elif protocol == 'INSERT_AFTER':
            lines.insert(marker_line + 1, code + '\n')
        elif protocol == 'REPLACE_LINE':
            lines[marker_line] = code + '\n'
        else:
            print(f"  [BRIDGE] 未知协议: {protocol}")
            return False
        
        with open(full_path, 'w') as f:
            f.writelines(lines)
        
        print(f"  [BRIDGE] 注入成功: '{marker_name}' ({protocol})")
        return True
    
    def generate_injection_code(self, marker_name: str,
                                 custom_code: str | None = None) -> str | None:
        """根据标记名生成合适的注入代码"""
        
        # 已知标记的推荐注入内容
        marker_templates: dict[str, str] = {
            "instinct_return_before": (
                '    if (in->code_mod_success == 1) '
                'inst.curiosity += 0.12f * cw;'
            ),
            "engine_include_insert": (
                '#include <sys/time.h>'
            ),
            "engine_rounds_insert": (
                'static int total_rounds = 0; total_rounds++;'
            ),
            "sandbox_devtools_insert": (
                '"docker", "podman", "flatpak", "pip3", '
                '"npm", "cargo", "gdb", "valgrind", "strace", "perf",'
            ),
        }
        
        if custom_code:
            return custom_code
        
        return marker_templates.get(marker_name)


# ── MCTS 动作桥接 ────────────────────────────────────────────

class MCTSBridge:
    """将 Python 侧的 MCTS 策略发送到 C 引擎"""
    
    # 从 mcts.h 映射动作编号到名称
    ACTION_NAMES: dict[int, str] = {
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
    
    def __init__(self) -> None:
        self.api: Any | None = None
        self._try_init_api()
    
    def _try_init_api(self) -> None:
        """尝试初始化 TorkAPI"""
        try:
            sys.path.insert(0, os.path.join(BASE, 'api'))
            from tork_api import TorkAPI
            self.api = TorkAPI()
            self.api.timeout = 10
        except Exception:
            self.api = None
    
    def send_mcts_action(self, mcts_type: int, param: int = 0) -> bool:
        """
        通过 persist/cloud_mcts_action.json 向 C 引擎发送 MCTS 动作。
        
        C 侧的 mutation_guide.c 或 scheduler.c 在 idle tick 中
        检查该文件是否存在，读取后作为 mg_recommend 的输入。
        这建立了 Python 云端 → C 引擎的真实单向通信通道。
        """
        import json as _json
        action_name: str = self.ACTION_NAMES.get(mcts_type, f"UNKNOWN({mcts_type})")
        action_file: str = os.path.join(BASE, "persist", "cloud_mcts_action.json")
        
        action_data: dict[str, object] = {
            "mcts_type": mcts_type,
            "param": param,
            "action_name": action_name,
            "timestamp": __import__("time").time(),
        }
        
        try:
            os.makedirs(os.path.dirname(action_file), exist_ok=True)
            with open(action_file, "w") as f:
                _json.dump(action_data, f)
            print(f"  [BRIDGE] MCTS {action_name} → 写入 cloud_mcts_action.json")
            return True
        except Exception as e:
            print(f"  [BRIDGE] MCTS 写入失败: {e}")
            return False
    
    def generate_mcts_action_from_strategy(self, strategy: dict[str, Any]) -> bool:
        """从策略字典执行 MCTS 动作"""
        params: dict[str, Any] = strategy.get("action_params", {})
        mcts_type: int = params.get("mcts_type", -1)
        mcts_param: int = params.get("param", 0)
        return self.send_mcts_action(mcts_type, mcts_param)


# ── code_modifier 桥接 ───────────────────────────────────────

class CodeModifierBridge:
    """桥接到 code_modifier.c 的 5 个实现动作"""
    
    def __init__(self) -> None:
        pass
    
    def replace_operand(self, asm_file: str, func_name: str,
                        old_op: str, new_op: str,
                        occurrence: int = 1) -> bool:
        """
        替换 ASM 文件中的操作数。
        通过临时 C 程序调用 code_modifier.c 的 asm_replace_operand。
        """
        full_path: str = os.path.join(BASE, asm_file.lstrip("./"))
        if not os.path.exists(full_path):
            print(f"  [BRIDGE] ASM 文件不存在: {full_path}")
            return False
        
        with open(full_path, 'r') as f:
            content: str = f.read()
        
        # 直接字符串替换（比编译调用 code_modifier 更轻量）
        # 在指定函数体内进行替换
        func_start: int = content.find(f"{func_name}:")
        if func_start == -1:
            print(f"  [BRIDGE] 函数 {func_name} 未找到")
            return False
        
        # 找到函数体结束（下一个非 .L 标签）
        body: str = content[func_start:]
        lines: list[str] = body.split('\n')
        
        count: int = 0
        for i, line in enumerate(lines):
            if old_op in line:
                count += 1
                if count == occurrence:
                    lines[i] = line.replace(old_op, new_op, 1)
                    content_new: str = '\n'.join(lines)
                    with open(full_path, 'w') as f:
                        f.write(content_new)
                    print(f"  [BRIDGE] asm_replace_operand: "
                          f"{old_op}→{new_op} 在 {func_name}+{i}")
                    return True
        
        print(f"  [BRIDGE] 操作数 {old_op} 在 {func_name} 中未找到")
        return False
    
    def delete_nop(self, asm_file: str, func_name: str) -> bool:
        """删除函数中的 NOP 指令"""
        # 搜索 nop 行并删除
        full_path: str = os.path.join(BASE, asm_file.lstrip("./"))
        with open(full_path, 'r') as f:
            lines: list[str] = f.readlines()
        
        in_func: bool = False
        new_lines: list[str] = []
        deleted: int = 0
        
        for line in lines:
            stripped: str = line.strip()
            
            if stripped.startswith(f"{func_name}:"):
                in_func = True
                new_lines.append(line)
                continue
            
            if in_func:
                # 下一个非 .L 标签表示函数结束
                if stripped and not stripped.startswith('.') and ':' in stripped:
                    in_func = False
                    new_lines.append(line)
                    continue
                
                # 检查是否是 NOP 指令
                if stripped.startswith('nop') or stripped.startswith('nopw') or \
                   stripped.startswith('nopl'):
                    deleted += 1
                    continue
            
            new_lines.append(line)
        
        if deleted > 0:
            with open(full_path, 'w') as f:
                f.writelines(new_lines)
            print(f"  [BRIDGE] asm_delete_nop: 在 {func_name} 中删除了 {deleted} 条 NOP")
            return True
        
        print(f"  [BRIDGE] {func_name} 中没有 NOP 指令")
        return False
    
    def delete_dead(self, asm_file: str, func_name: str) -> bool:
        """删除函数中 ret 之后的死代码"""
        full_path: str = os.path.join(BASE, asm_file.lstrip("./"))
        with open(full_path, 'r') as f:
            lines: list[str] = f.readlines()
        
        in_func: bool = False
        found_ret: bool = False
        new_lines: list[str] = []
        deleted: int = 0
        
        for line in lines:
            stripped: str = line.strip()
            
            if stripped.startswith(f"{func_name}:"):
                in_func = True
                found_ret = False
                new_lines.append(line)
                continue
            
            if in_func:
                # 函数结束
                if stripped and not stripped.startswith('.') and ':' in stripped and \
                   not stripped.startswith('.L'):
                    in_func = False
                    new_lines.append(line)
                    continue
                
                if not found_ret:
                    if stripped.startswith('ret') or stripped.startswith('retq'):
                        found_ret = True
                    new_lines.append(line)
                else:
                    # ret 之后：跳过标签、注释、非指令行，删除指令行
                    if stripped and line.startswith('\t') and \
                       not line.startswith('\t.') and \
                       not line.startswith('\t#'):
                        deleted += 1
                        continue
                    new_lines.append(line)
                continue
            
            new_lines.append(line)
        
        if deleted > 0:
            with open(full_path, 'w') as f:
                f.writelines(new_lines)
            print(f"  [BRIDGE] asm_delete_dead: 在 {func_name} 中删除了 {deleted} 条死代码")
            return True
        
        print(f"  [BRIDGE] {func_name} 中没有死代码")
        return False
    
    def swap_regs(self, asm_file: str, func_name: str,
                  reg1: str, reg2: str) -> bool:
        """交换函数中的寄存器（MCTS_MOD_SWAP_REGS）"""
        full_path: str = os.path.join(BASE, asm_file.lstrip("./"))
        with open(full_path, 'r') as f:
            content: str = f.read()
        
        func_start: int = content.find(f"{func_name}:")
        if func_start == -1:
            print(f"  [BRIDGE] 函数 {func_name} 未找到")
            return False
        
        body: str = content[func_start:]
        lines: list[str] = body.split('\n')
        
        swapped: int = 0
        for i, line in enumerate(lines):
            if reg1 in line:
                # 只替换完整的寄存器引用（前后非字母数字）
                # 保护：%eax 替换为 %ebx 时不能将 %eax 替换为 %ebx 再
                # 将 %ebx（含新的）替换为 %eax
                new_line: str = line.replace(reg1, f"__TEMP_REG__", 1)
                new_line = new_line.replace(reg2, reg1, 1)
                new_line = new_line.replace("__TEMP_REG__", reg2)
                lines[i] = new_line
                swapped += 1
        
        if swapped > 0:
            content_new: str = '\n'.join(lines)
            with open(full_path, 'w') as f:
                f.write(content_new)
            print(f"  [BRIDGE] asm_swap_regs: 在 {func_name} 中交换了 "
                  f"{reg1}↔{reg2} ({swapped}处)")
            return True
        
        print(f"  [BRIDGE] {func_name} 中未找到寄存器 {reg1}/{reg2}")
        return False


# ── 统一执行器 ──────────────────────────────────────────────

class MutationExecutor:
    """统一执行所有类型的变异策略"""
    
    def __init__(self) -> None:
        self.param_mod: ParamModulator = ParamModulator()
        self.marker_inj: MarkerInjector = MarkerInjector()
        self.mcts_bridge: MCTSBridge = MCTSBridge()
        self.code_mod: CodeModifierBridge = CodeModifierBridge()
    
    def execute(self, strategy: dict[str, Any]) -> bool:
        """
        执行一个策略字典。
        
        Args:
            strategy: 策略字典（来自 strategy_generator 或 evolution.py）
        
        Returns:
            True 如果执行成功
        """
        action_type: str = strategy.get("action_type", "")
        params: dict[str, Any] = strategy.get("action_params", {})
        target_file: str = strategy.get("target_file", "")
        
        print(f"  [EXEC] {strategy.get('description', '?')}")
        
        if action_type == "mod_param":
            return self.param_mod.modulate(
                target_file,
                params.get("param", ""),
                params.get("delta", 0),
                params.get("method", "struct_field"),
            )
        
        elif action_type == "inject_marker":
            return self.marker_inj.inject(
                target_file,
                params.get("marker", ""),
                params.get("code", ""),
                params.get("protocol", "INSERT_BEFORE"),
            )
        
        elif action_type == "mcts_action":
            return self.mcts_bridge.generate_mcts_action_from_strategy(strategy)
        
        elif action_type == "code_modifier":
            # code_modifier 子类型
            modifier_type: str = params.get("modifier_type", "")
            asm_file: str = params.get("asm_file", "src/core/tork_core.asm")
            func_name: str = params.get("func_name", "")
            
            if modifier_type == "replace_operand":
                return self.code_mod.replace_operand(
                    asm_file, func_name,
                    params.get("old_op", ""),
                    params.get("new_op", ""),
                    params.get("occurrence", 1),
                )
            elif modifier_type == "delete_nop":
                return self.code_mod.delete_nop(asm_file, func_name)
            elif modifier_type == "delete_dead":
                return self.code_mod.delete_dead(asm_file, func_name)
            elif modifier_type == "swap_regs":
                return self.code_mod.swap_regs(
                    asm_file, func_name,
                    params.get("reg1", ""),
                    params.get("reg2", ""),
                )
            else:
                print(f"  [EXEC] 未知 code_modifier 子类型: {modifier_type}")
                return False
        
        else:
            print(f"  [EXEC] 未知 action_type: {action_type}")
            return False


# ── 快速测试 ────────────────────────────────────────────────

if __name__ == "__main__":
    exec: MutationExecutor = MutationExecutor()
    
    # 测试调参
    test_strategy: dict[str, Any] = {
        "action_type": "mod_param",
        "target_file": "src/instinct/instinct.c",
        "action_params": {
            "param": "curiosity_weight",
            "delta": 15,
            "method": "struct_field",
        },
    }
    result: bool = exec.execute(test_strategy)
    print(f"测试调参: {'✓' if result else '✗'}")
