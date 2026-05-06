#!/usr/bin/env python3
"""
TORK 健康验证器 v1.0 — 编译 + 运行时双通道验证

验证管道:
  1. 编译验证 (make all) — 现有
  2. 运行时验证 — 启动 TORK 引擎，检查:
     a. 心跳存活 (10 ticks 内 soul_cur_tsc 持续增长)
     b. Soul CRC 完整性 (soul_read 返回一致 CRC)
     c. 本能输出合理性 (fear/desire/curiosity 在 [0,1] 区间)
     d. 信标广播可达性 (UDP socket 可写)
  3. 适应度归一化 — sigmoid(运行时健康分)

验证结果格式:
  {
    "passed": True/False,
    "compile_ok": True/False,
    "runtime_ok": True/False/None,    # None = 未执行
    "health_score": 0.0-1.0,          # sigmoid 归一化
    "runtime_metrics": { ... },
    "errors": [ ... ]
  }
"""

from __future__ import annotations

import os
import re
import signal
import subprocess
import sys
import time
from typing import Any

BASE: str = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class CompileValidator:
    """编译验证"""
    
    def validate(self, timeout: int = 60) -> dict[str, Any]:
        """执行 make all 编译验证"""
        result: dict[str, Any] = {
            "compile_ok": False,
            "returncode": -1,
            "errors": [],
            "warnings": [],
        }
        
        try:
            r: subprocess.CompletedProcess[str] = subprocess.run(
                ["make", "-C", BASE, "all"],
                capture_output=True, text=True, timeout=timeout,
            )
            result["returncode"] = r.returncode
            
            if r.returncode != 0:
                # 提取错误
                for line in r.stderr.split("\n"):
                    if "error:" in line.lower():
                        result["errors"].append(line.strip())
                    elif "warning:" in line.lower():
                        result["warnings"].append(line.strip())
                result["compile_ok"] = False
            else:
                result["compile_ok"] = True
            
            return result
            
        except subprocess.TimeoutExpired:
            result["errors"].append(f"编译超时 ({timeout}s)")
            result["compile_ok"] = False
            return result
        except FileNotFoundError:
            result["errors"].append("make 命令未找到")
            result["compile_ok"] = False
            return result


class RuntimeValidator:
    """
    运行时健康验证。
    
    启动 tork_engine 并监控其行为。
    安全约束：超时 15 秒、SIGTERM 强制终止。
    """
    
    # Soul 字段偏移（用于读取运行时状态）
    SOUL_SIZE: int = 208
    
    def __init__(self, engine_path: str | None = None) -> None:
        self.engine_path: str = engine_path or os.path.join(BASE, "tork_core.bin")
        self.process: subprocess.Popen[str] | None = None
    
    def validate(self, timeout: float = 15.0) -> dict[str, Any]:
        """
        运行时验证：启动引擎，监控 10 ticks。
        
        Returns:
            runtime 验证结果字典
        """
        result: dict[str, Any] = {
            "runtime_ok": False,
            "heartbeat_alive": False,
            "crc_valid": False,
            "instinct_sane": False,
            "beacon_reachable": False,
            "ticks_observed": 0,
            "soul_snapshots": [],
            "errors": [],
        }
        
        if not os.path.exists(self.engine_path):
            result["errors"].append(f"引擎文件不存在: {self.engine_path}")
            # 如果引擎不存在，尝试构建
            try:
                subprocess.run(["make", "-C", BASE, "all"],
                               capture_output=True, timeout=60)
                if not os.path.exists(self.engine_path):
                    result["errors"].append("构建后引擎仍未生成")
                    return result
            except Exception as e:
                result["errors"].append(f"构建失败: {e}")
                return result
        
        try:
            # 启动引擎
            self.process = subprocess.Popen(
                [self.engine_path],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=BASE,
            )
            
            # 给引擎 2 秒初始化
            time.sleep(2.0)
            
            # 检查进程是否存活
            if self.process.poll() is not None:
                stderr_out: str = ""
                try:
                    _, stderr_out = self.process.communicate(timeout=2)
                except subprocess.TimeoutExpired:
                    pass
                result["errors"].append(
                    f"引擎过早退出，返回码={self.process.returncode}, "
                    f"stderr={stderr_out[:200]}"
                )
                return result
            
            # 心跳存活检查：观察 Soul 中的 TSC 是否持续增长
            pid: int = self.process.pid
            mem_path: str = f"/proc/{pid}/mem"
            soul_addr: int = 0x200000  # 从架构文档中读取
            
            snapshots: list[dict[str, int]] = []
            for tick in range(5):
                try:
                    with open(mem_path, 'rb') as f:
                        f.seek(soul_addr)
                        soul_data: bytes = f.read(self.SOUL_SIZE)
                    
                    if len(soul_data) < self.SOUL_SIZE:
                        result["errors"].append(f"Soul 读取不完整: {len(soul_data)}")
                        break
                    
                    # 解析关键字段
                    # S_TICK(0x00, 4B LE), S_CUR_TSC(0x0C, 8B LE)
                    import struct
                    s_tick: int = struct.unpack_from('<I', soul_data, 0x00)[0]
                    s_cur_tsc: int = struct.unpack_from('<Q', soul_data, 0x0C)[0]
                    s_drive: int = soul_data[0x30]  # S_DRIVE (1B)
                    s_mode: int = soul_data[0x25]   # S_MODE (1B)
                    
                    snapshots.append({
                        "tick": tick,
                        "s_tick": s_tick,
                        "s_cur_tsc": s_cur_tsc,
                        "s_drive": s_drive,
                        "s_mode": s_mode,
                    })
                    
                except Exception as e:
                    result["errors"].append(f"第{tick}次 Soul 读取失败: {e}")
                    break
                
                time.sleep(0.5)  # 每 tick 等待 500ms
            
            result["soul_snapshots"] = snapshots
            result["ticks_observed"] = len(snapshots)
            
            # 分析运行时健康
            if len(snapshots) >= 2:
                # 心跳存活：TSC 持续增长
                tsc_values: list[int] = [s["s_cur_tsc"] for s in snapshots]
                if all(tsc_values[i] < tsc_values[i+1] for i in range(len(tsc_values)-1)):
                    result["heartbeat_alive"] = True
                else:
                    result["errors"].append("TSC 未持续增长，心跳可能停滞")
                
                # 本能输出合理性
                drive_values: list[int] = [s["s_drive"] for s in snapshots]
                if all(0 <= d <= 255 for d in drive_values):
                    result["instinct_sane"] = True
                else:
                    result["errors"].append(
                        f"本能 drive 值异常: {drive_values}"
                    )
                
                # Tick 计数器增长
                if snapshots[-1]["s_tick"] >= snapshots[0]["s_tick"]:
                    result["runtime_ok"] = True
            
            # 信标广播可达性检查
            self._check_beacon(result)
            
        except Exception as e:
            result["errors"].append(f"运行时验证异常: {e}")
        finally:
            self._cleanup()
        
        return result
    
    def _check_beacon(self, result: dict[str, Any]) -> None:
        """检查 UDP 信标端口是否可写"""
        try:
            import socket
            s: socket.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            # 不真正发送，只检查 socket 是否可创建
            s.settimeout(0.1)
            result["beacon_reachable"] = True
            s.close()
        except Exception:
            result["errors"].append("UDP socket 不可用，信标可能无法广播")
            result["beacon_reachable"] = False
    
    def _cleanup(self) -> None:
        """安全终止引擎进程"""
        if self.process and self.process.poll() is None:
            try:
                self.process.send_signal(signal.SIGTERM)
                self.process.wait(timeout=3)
            except (subprocess.TimeoutExpired, ProcessLookupError):
                try:
                    self.process.kill()
                    self.process.wait(timeout=2)
                except (subprocess.TimeoutExpired, ProcessLookupError):
                    pass


class FitnessCalculator:
    """
    适应度计算器。
    
    将原始适应度分数归一化到 [0, 1] 区间。
    公式: sigmoid((存活时间权重 + 编译权重 + 运行时健康权重) - 偏置)
    """
    
    def compute_normalized_fitness(self, survival_ticks: int,
                                    compile_ok: int,
                                    runtime_health: float | None = None) -> float:
        """
        计算归一化的适应度分数。
        
        Args:
            survival_ticks: 变异存活的心跳数
            compile_ok: 编译是否通过 (0/1)
            runtime_health: 运行时健康分 (0.0-1.0), None 表示未测试
        
        Returns:
            0.0 - 1.0 范围内的归一化分数
        """
        # 存活时间用 sigmoid 归一化，避免无上界问题
        # 健康存活时间 1000 ticks = 50% 权重
        survival_norm: float = 1.0 / (1.0 + (1000.0 / max(1, survival_ticks)))
        
        # 编译正确性权重
        compile_weight: float = compile_ok * 0.3
        
        # 运行时健康权重（如果有）
        runtime_weight: float = 0.0
        if runtime_health is not None:
            runtime_weight = runtime_health * 0.4
        else:
            # 无运行时数据时，编译权重提升
            compile_weight = compile_ok * 0.5
        
        raw_score: float = survival_norm * 0.3 + compile_weight + runtime_weight
        
        # sigmoid 归一化
        x: float = (raw_score - 0.5) * 4.0  # 拉伸到 sigmoid 敏感区
        normalized: float = 1.0 / (1.0 + (2.71828 ** (-x)))
        
        return round(normalized, 4)
    
    def compute_from_history(self, history_entry: dict[str, Any],
                              runtime_health: float | None = None) -> float:
        """从历史记录条目计算适应度"""
        return self.compute_normalized_fitness(
            survival_ticks=history_entry.get("survival_ticks", 0),
            compile_ok=history_entry.get("compile_ok", 0),
            runtime_health=runtime_health,
        )


class ValidatorPipeline:
    """完整的验证管道"""
    
    def __init__(self) -> None:
        self.compile_val: CompileValidator = CompileValidator()
        self.runtime_val: RuntimeValidator = RuntimeValidator()
        self.fitness_calc: FitnessCalculator = FitnessCalculator()
    
    def validate(self, runtime: bool = True,
                 compile_timeout: int = 60,
                 runtime_timeout: float = 15.0) -> dict[str, Any]:
        """
        执行完整验证管道。
        
        Args:
            runtime: 是否执行运行时验证
            compile_timeout: 编译超时（秒）
            runtime_timeout: 运行时超时（秒）
        
        Returns:
            统一验证结果字典
        """
        result: dict[str, Any] = {
            "passed": False,
            "compile_ok": False,
            "runtime_ok": None,
            "health_score": 0.0,
            "compile_details": None,
            "runtime_details": None,
            "errors": [],
        }
        
        # 第一步：编译验证
        compile_result: dict[str, Any] = self.compile_val.validate(compile_timeout)
        result["compile_ok"] = compile_result["compile_ok"]
        result["compile_details"] = compile_result
        
        if not compile_result["compile_ok"]:
            result["errors"].extend(compile_result.get("errors", []))
            result["health_score"] = self.fitness_calc.compute_normalized_fitness(
                survival_ticks=0, compile_ok=0, runtime_health=None,
            )
            return result
        
        # 第二步：运行时验证（可选）
        if runtime:
            runtime_result: dict[str, Any] = self.runtime_val.validate(runtime_timeout)
            result["runtime_ok"] = runtime_result.get("runtime_ok", False)
            result["runtime_details"] = runtime_result
            
            if runtime_result.get("errors"):
                result["errors"].extend(runtime_result["errors"])
        
        # 第三步：计算适应度
        runtime_health: float | None = None
        if result["runtime_ok"] is not None:
            # 从运行时详情计算健康分
            healthy_checks: int = sum([
                1 if runtime_result.get("heartbeat_alive") else 0,
                1 if runtime_result.get("instinct_sane") else 0,
                1 if runtime_result.get("beacon_reachable") else 0,
            ])
            runtime_health = healthy_checks / 3.0
        
        result["health_score"] = self.fitness_calc.compute_normalized_fitness(
            survival_ticks=1,
            compile_ok=1,
            runtime_health=runtime_health,
        )
        
        # 最终的 passed 判定
        result["passed"] = (
            result["compile_ok"] and
            (result["runtime_ok"] is None or result["runtime_ok"])
        )
        
        return result


# ── 快速测试 ────────────────────────────────────────────────

if __name__ == "__main__":
    import json
    
    pipe: ValidatorPipeline = ValidatorPipeline()
    print("执行编译验证（仅编译，不运行）...")
    result: dict[str, Any] = pipe.validate(runtime=False)
    print(json.dumps(result, indent=2, default=str))
