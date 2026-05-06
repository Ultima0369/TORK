#!/usr/bin/env python3
"""
TORK 自我进化引擎 v3.0 — DeepSeek 战略指导 + 适应度反馈循环

混合架构：
  - strategy_generator: DeepSeek 驱动策略生成 + 失败模式反馈
  - code_bridge: Python ↔ C 变异执行（参数/标记/MCTS/code_modifier）
  - validator: 编译 + 运行时双通道验证 + sigmoid 适应度归一化
  - 适应度反馈：sigmod(存活时间×0.3 + 编译×0.3 + 运行时×0.4)

与 v2.3 的差异：
  - DeepSeek 不再只是装饰品：它生成 JSON 策略，影响实际变异
  - 策略空间从 10 个硬编码模板扩展到无限（LLM 生成 + MCTS 11 动作桥接）
  - 适应度公式修正为 sigmoid 归一化，消除"活得久=分高"的偏差
  - 新增运行时验证（心跳/本能/信标），编译通过不再是唯一标准
  - 新增失败模式分析：失败反馈回 DeepSeek 的 prompt，避免重复犯错
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import time
from typing import Any, Callable

BASE: str = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(BASE, 'api'))
sys.path.insert(0, os.path.join(BASE, 'cloud'))

EVOLUTION_LOG: str = os.path.join(BASE, "persist", "evolution.json")
MUTATION_DIR: str = os.path.join(BASE, "persist", "mutations")
FITNESS_LOG: str = os.path.join(BASE, "persist", "fitness.json")


# ── 延迟导入（避免循环依赖） ─────────────────────────────────

class TorkEvolution:
    api: Any | None
    history: dict[str, Any]
    fitness: dict[str, Any]
    last_assessment: dict[str, Any] | None
    
    # 子模块（延迟初始化）
    _strategy_gen: Any | None = None
    _executor: Any | None = None
    _validator: Any | None = None
    _failure_analyzer: Any | None = None
    
    def __init__(self) -> None:
        os.makedirs(MUTATION_DIR, exist_ok=True)
        self.history = self._load_json(EVOLUTION_LOG, {
            "generation": 0, "mutations": [],
            "successes": 0, "failures": 0,
        })
        self.fitness = self._load_json(FITNESS_LOG, {
            "history": [], "best_mutagen": None, "best_score": 0.0,
            "frozen": {}, "endangered": {},
        })
        self.last_assessment: dict[str, Any] | None = None
        
        try:
            from tork_api import TorkAPI
            self.api = TorkAPI()
            self.api.timeout = 30
        except Exception:
            self.api = None
    
    def _lazy_init_modules(self) -> None:
        """延迟初始化子模块（避免导入时循环依赖）"""
        if self._strategy_gen is not None:
            return
        
        from cloud.strategy_generator import StrategyGenerator, FailureAnalyzer
        from cloud.code_bridge import MutationExecutor
        from cloud.validator import ValidatorPipeline
        
        self._strategy_gen = StrategyGenerator(api=self.api)
        self._executor = MutationExecutor()
        self._validator = ValidatorPipeline()
        self._failure_analyzer = FailureAnalyzer()
    
    def _load_json(self, path: str, default: dict[str, Any]) -> dict[str, Any]:
        if os.path.exists(path):
            try:
                with open(path) as f:
                    return json.load(f)
            except (json.JSONDecodeError, OSError):
                pass
        return default
    
    def _save_json(self, path: str, data: dict[str, Any]) -> None:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w') as f:
            json.dump(data, f, indent=2)
    
    def _save_history(self) -> None:
        self._save_json(EVOLUTION_LOG, self.history)
    
    def _save_fitness(self) -> None:
        self._save_json(FITNESS_LOG, self.fitness)
    
    def assess_self(self) -> dict[str, Any]:
        """自我评估 — 与 v2.3 兼容"""
        print("TORK self-assessment...")
        
        source_files: list[str] = [
            "src/core/tork_core.asm", "src/engine/tork_engine.c",
            "src/instinct/instinct.c", "src/instinct/instinct.h",
            "src/code/code_reader.c", "src/code/code_modifier.c",
            "src/code/code_modifier.h", "src/engine/soul_access.h",
            "src/engine/blackboard.c", "src/engine/calibrator.c",
            "src/engine/fission.c", "src/engine/idler.c",
            "src/engine/inductor.c", "src/engine/persistor.c",
            "src/engine/monitor.c", "src/sandbox/sandbox.c",
            "src/sandbox/sandbox.h", "src/install/agreement.c",
            "src/install/agreement.h", "cloud/cloud_protocol.py",
            "cloud/evolution.py",
        ]
        
        guide_rec: dict[str, int] | None = None
        guide_file: str = os.path.join(BASE, "persist", "mutation_guide.bin")
        if os.path.exists(guide_file):
            import struct
            try:
                with open(guide_file, 'rb') as gf:
                    data: bytes = gf.read()
                    if len(data) > 0x8F8:
                        guide_rec = {
                            'total_attempts': struct.unpack_from('I', data, 0x8F0)[0],
                            'total_successes': struct.unpack_from('I', data, 0x8F4)[0],
                        }
            except (json.JSONDecodeError, OSError):
                pass
        
        files_info: list[dict[str, Any]] = []
        total_lines: int = 0
        for fname in source_files:
            fpath: str = os.path.join(BASE, fname)
            if os.path.exists(fpath):
                with open(fpath) as f:
                    content: str = f.read()
                lines: int = len(content.split("\n"))
                files_info.append({"file": fname, "lines": lines})
                total_lines += lines
        
        assessment: dict[str, Any] = {
            "generation": self.history["generation"],
            "total_lines": total_lines,
            "total_files": len(files_info),
            "success_rate": (
                self.history["successes"] /
                max(1, self.history["successes"] + self.history["failures"])
            ),
            "total_mutations": len(self.history["mutations"]),
            "guide": guide_rec,
            "files": files_info,
            "timestamp": time.time(),
        }
        self.last_assessment = assessment
        return assessment
    
    def _pick_mutation_strategy(self, assessment: dict[str, Any]) -> dict[str, Any]:
        """
        选择变异策略。
        
        新流程：
          1. strategy_generator 生成候选策略列表（含 DeepSeek 生成 + 已知参数微调）
          2. 过滤掉被冷冻的策略
          3. 适应度加权选择
        
        与 v2.3 的关键区别：策略来源从 10 个硬编码模板变为
        DeepSeek JSON 生成 + 已知参数轮换。
        """
        self._lazy_init_modules()
        gen: int = assessment["generation"]
        
        # 从 strategy_generator 获取候选策略
        recent: list[dict[str, Any]] = self.history["mutations"][-5:]
        all_candidates: list[dict[str, Any]] = self._strategy_gen.generate(
            assessment, recent
        )
        
        if not all_candidates:
            # 兜底：返回一个安全的参数微调
            from cloud.strategy_generator import KNOWN_PARAMS
            param_names: list[str] = sorted(KNOWN_PARAMS.keys())
            param: str = param_names[gen % len(param_names)]
            return {
                "mutagen": f"emergency_{param}_{gen}",
                "description": f"兜底: {param} +10",
                "target_file": KNOWN_PARAMS[param]["file"],
                "action_type": "mod_param",
                "action_params": {
                    "param": param,
                    "delta": 10,
                    "method": KNOWN_PARAMS[param]["type"],
                },
                "confidence": 0.2,
                "rationale": "strategy_generator 未返回有效策略，采用兜底调参",
            }
        
        # 冷冻过滤
        frozen: dict[str, int] = self.fitness.get("frozen", {})
        available: list[dict[str, Any]] = []
        for s in all_candidates:
            mid: str = s.get("mutagen", "unknown")
            if mid not in frozen or frozen[mid] <= gen:
                if mid in frozen and frozen[mid] <= gen:
                    del frozen[mid]
                available.append(s)
        
        if not available:
            available = all_candidates
        
        # 适应度加权选择
        mutagen_scores: dict[str, float] = self._compute_mutagen_scores()
        
        # 精英保留（10% 概率选最佳）
        import random
        if mutagen_scores:
            sorted_avail: list[dict[str, Any]] = sorted(
                available,
                key=lambda s: mutagen_scores.get(s.get("mutagen", ""), 0),
                reverse=True,
            )
            elite_cutoff: int = max(1, len(sorted_avail) // 10)
            if random.random() < 0.1 and sorted_avail:
                return random.choice(sorted_avail[:elite_cutoff])
        
        # 90%：加权随机
        weights: list[float] = [
            max(mutagen_scores.get(s.get("mutagen", ""), 1.0), 0.01)
            for s in available
        ]
        total: float = sum(weights)
        r: float = random.uniform(0, total)
        cumulative: float = 0.0
        for i, w in enumerate(weights):
            cumulative += w
            if r <= cumulative:
                return available[i]
        
        return available[gen % len(available)]
    
    def _compute_mutagen_scores(self) -> dict[str, float]:
        """
        适应度计算 — v3.0 使用 sigmoid 归一化。
        
        公式: sigmoid((存活归一化×0.3 + 编译×0.3 + 运行时×0.4) - 0.5) × 4
        """
        from cloud.validator import FitnessCalculator
        calc: FitnessCalculator = FitnessCalculator()
        
        scores: dict[str, float] = {}
        for entry in self.fitness["history"]:
            m: str = entry.get("mutagen", "unknown")
            survived: bool = entry.get("survived", False)
            
            score: float = calc.compute_from_history(entry)
            
            if m not in scores:
                scores[m] = 0.0
            if survived:
                scores[m] += score
            else:
                scores[m] -= score * 0.5  # 失败惩罚
        
        return scores
    
    def _mark_endangered(self, scores: dict[str, float], gen: int) -> None:
        """每 100 轮：最低 10% 标记濒危"""
        if "endangered" not in self.fitness:
            self.fitness["endangered"] = {}
        if not scores:
            return
        sorted_items: list[tuple[str, float]] = sorted(
            scores.items(), key=lambda x: x[1]
        )
        cutoff: int = max(1, len(sorted_items) // 10)
        for m, _ in sorted_items[:cutoff]:
            self.fitness["endangered"][m] = gen
        self._save_fitness()
    
    def _prune_endangered(self, gen: int) -> None:
        """200 轮后濒危未改善则移除"""
        if "endangered" not in self.fitness:
            return
        to_remove: list[str] = []
        for m, mark_gen in list(self.fitness["endangered"].items()):
            if gen - mark_gen >= 200:
                scores: dict[str, float] = self._compute_mutagen_scores()
                if scores.get(m, 0) <= 0:
                    to_remove.append(m)
        for m in to_remove:
            del self.fitness["endangered"][m]
            self.fitness["history"] = [
                e for e in self.fitness["history"]
                if e.get("mutagen") != m
            ]
            for fname in os.listdir(MUTATION_DIR):
                if m in fname:
                    try:
                        os.unlink(os.path.join(MUTATION_DIR, fname))
                    except OSError:
                        pass
            print(f"  [EVO] 已剪枝灭绝策略: {m}")
        if to_remove:
            self._save_fitness()
    
    def _apply_fitness_decay(self, mutagen: str, factor: float) -> None:
        """指数衰减（与 v2.3 兼容）"""
        HALF_LIFE: int = 3
        for entry in self.fitness["history"]:
            if entry.get("mutagen") == mutagen and not entry.get("compile_ok", True):
                consecutive_fails: int = 0
                for e in reversed(self.fitness["history"]):
                    if e.get("mutagen") == mutagen:
                        if not e.get("compile_ok", True):
                            consecutive_fails += 1
                        else:
                            break
                    else:
                        break
                decay: float = 0.5 ** (consecutive_fails / HALF_LIFE)
                entry["fitness"] = entry.get("fitness", 1.0) * decay
        self._save_fitness()
    
    def _check_freeze_strategy(self, mutagen: str, gen: int) -> None:
        """连续 3 次编译失败 → 冷冻 100 轮"""
        if "frozen" not in self.fitness:
            self.fitness["frozen"] = {}
        fail_streak: int = 0
        for entry in reversed(self.fitness["history"]):
            if entry.get("mutagen") == mutagen:
                if not entry.get("survived", False):
                    fail_streak += 1
                else:
                    break
            else:
                break
        if fail_streak >= 3:
            self.fitness["frozen"][mutagen] = gen + 100
            self._save_fitness()
            print(f"  [EVO] 策略 {mutagen} 冷冻 100 轮")
    
    def apply_mutation(self, strategy: dict[str, Any]) -> bool:
        """
        应用变异。
        
        新流程：
          1. code_bridge 执行策略
          2. 编译验证
          3. 运行时验证（可选）
          4. 更新适应度历史
          5. DeepSeek 失败分析反馈
        """
        self._lazy_init_modules()
        
        if not strategy or not strategy.get("action_type"):
            print("  无效策略")
            return False
        
        filepath: str = os.path.join(BASE, strategy.get("target_file", ""))
        print(f"  变异: {strategy.get('description', '?')}")
        print(f"    类型: {strategy.get('action_type', '?')}")
        print(f"    文件: {strategy.get('target_file', '?')}")
        
        mutation_record: dict[str, Any] = {
            "generation": self.history["generation"],
            "timestamp": time.time(),
            "file": strategy.get("target_file", ""),
            "description": strategy.get("description", ""),
            "mutagen": strategy.get("mutagen", "unknown"),
            "action_type": strategy.get("action_type", ""),
            "action_params": strategy.get("action_params", {}),
        }
        
        # 文件级备份
        backup_path: str | None = None
        if os.path.exists(filepath):
            backup_path = filepath + ".evo_bak"
            shutil.copy2(filepath, backup_path)
        
        try:
            # 执行策略
            success: bool = self._executor.execute(strategy)
            
            if not success:
                print(f"  策略执行失败")
                if backup_path and os.path.exists(backup_path):
                    shutil.copy2(backup_path, filepath)
                    os.unlink(backup_path)
                mutation_record["result"] = "apply_failed"
                self._record_failure(mutation_record, strategy)
                return False
            
        except Exception as e:
            print(f"  异常: {e}")
            if backup_path and os.path.exists(backup_path):
                shutil.copy2(backup_path, filepath)
                os.unlink(backup_path)
            mutation_record["result"] = f"exception: {e}"
            self._record_failure(mutation_record, strategy)
            return False
        
        # 编译验证
        print("  编译验证...")
        compile_result: dict[str, Any] = self._validator.compile_val.validate()
        
        if not compile_result["compile_ok"]:
            errors: list[str] = compile_result.get("errors", [])
            for e in errors[:3]:
                print(f"    {e}")
            if backup_path and os.path.exists(backup_path):
                shutil.copy2(backup_path, filepath)
                os.unlink(backup_path)
            
            mutation_record["result"] = "compile_failed"
            mutagen: str = strategy.get("mutagen", "unknown")
            
            # 记录失败
            self.fitness["history"].append({
                "mutagen": mutagen,
                "gen": mutation_record["generation"],
                "timestamp": time.time(),
                "survived": False,
                "survival_ticks": 0,
                "compile_ok": 0,
            })
            
            self._apply_fitness_decay(mutagen, 0.5)
            self._check_freeze_strategy(mutagen, self.history["generation"])
            
            # 失败分析反馈
            error_text: str = "\n".join(errors) if errors else "unknown"
            self._failure_analyzer.analyze(mutation_record, error_text)
            
            self._save_fitness()
            self._save_history()
            return False
        
        # 运行时验证（可选、轻量）
        print("  运行时验证（5 ticks）...")
        runtime_result: dict[str, Any] | None = None
        try:
            runtime_result = self._validator.runtime_val.validate(timeout=10.0)
            if runtime_result.get("runtime_ok"):
                print(f"    心跳正常, "
                      f"tick={runtime_result['soul_snapshots'][-1]['s_tick']}")
            else:
                errs: list[str] = runtime_result.get("errors", [])
                for e in errs[:2]:
                    print(f"    ⚠ {e}")
        except Exception as e:
            print(f"    运行时验证跳过: {e}")
        
        # 清理备份
        if backup_path and os.path.exists(backup_path):
            os.unlink(backup_path)
        
        # 成功记录
        print(f"  成功! Gen {self.history['generation']} → "
              f"{self.history['generation'] + 1}")
        mutation_record["result"] = "success"
        self.history["mutations"].append(mutation_record)
        self.history["generation"] += 1
        self.history["successes"] += 1
        self._save_history()
        
        # 计算适应度
        mutagen = strategy.get("mutagen", "unknown")
        
        # 存活时间 = 自上次同 mutagen 成功以来的代数
        last_success_gen: int = -1
        for entry in reversed(self.fitness["history"]):
            if entry.get("mutagen") == mutagen and entry.get("survived"):
                last_success_gen = entry.get("gen", 0)
                break
        survival_ticks: int = (
            max(1, self.history["generation"] - last_success_gen)
            if last_success_gen >= 0 else 1
        )
        
        # 运行时健康分
        runtime_health: float | None = None
        if runtime_result and runtime_result.get("runtime_ok"):
            healthy: int = sum([
                1 if runtime_result.get("heartbeat_alive") else 0,
                1 if runtime_result.get("instinct_sane") else 0,
            ])
            runtime_health = healthy / 2.0
        
        from cloud.validator import FitnessCalculator
        calc: FitnessCalculator = FitnessCalculator()
        score: float = calc.compute_normalized_fitness(
            survival_ticks=survival_ticks,
            compile_ok=1,
            runtime_health=runtime_health,
        )
        
        self.fitness["history"].append({
            "mutagen": mutagen,
            "gen": mutation_record["generation"],
            "timestamp": time.time(),
            "survived": True,
            "survival_ticks": survival_ticks,
            "compile_ok": 1,
            "runtime_health": runtime_health,
            "fitness": score,
        })
        
        if score > self.fitness.get("best_score", 0.0):
            self.fitness["best_score"] = score
            self.fitness["best_mutagen"] = mutagen
        
        self._save_fitness()
        
        # Git 提交
        try:
            target_file: str = strategy.get("target_file", "")
            if target_file and os.path.exists(os.path.join(BASE, target_file)):
                subprocess.run(
                    ["git", "-C", BASE, "add", target_file],
                    capture_output=True, timeout=5,
                )
                subprocess.run(
                    ["git", "-C", BASE, "commit", "-m",
                     f"evo gen{self.history['generation']}: "
                     f"{strategy.get('description', '?')}"],
                    capture_output=True, timeout=5,
                )
        except Exception:
            pass
        
        return True
    
    def _record_failure(self, mutation_record: dict[str, Any],
                        strategy: dict[str, Any]) -> None:
        """记录失败并更新适应度"""
        self.history["mutations"].append(mutation_record)
        self.history["failures"] += 1
        mutagen: str = strategy.get("mutagen", "unknown")
        
        self.fitness["history"].append({
            "mutagen": mutagen,
            "gen": mutation_record["generation"],
            "timestamp": time.time(),
            "survived": False,
            "survival_ticks": 0,
        })
        
        self._apply_fitness_decay(mutagen, 0.5)
        self._check_freeze_strategy(mutagen, self.history["generation"])
        self._save_fitness()
        self._save_history()
    
    def evolve(self, rounds: int = 1) -> dict[str, Any]:
        """主进化循环"""
        print(f"\n{'='*60}")
        print(f"TORK Evolution Engine v3.0")
        print(f"   Generation: {self.history['generation']}")
        print(f"   Mutations: {len(self.history['mutations'])}")
        print(f"   Success: {self.history['successes']}/"
              f"{self.history['successes'] + self.history['failures']}")
        if self.fitness["best_mutagen"]:
            print(f"   Best: {self.fitness['best_mutagen']} "
                  f"(score {self.fitness['best_score']:.4f})")
        print(f"{'='*60}\n")
        
        for r in range(rounds):
            print(f"\n--- Round {r+1}/{rounds} ---")
            assessment: dict[str, Any] = self.assess_self()
            print(f"   {assessment['total_lines']} lines / "
                  f"{assessment['total_files']} files")
            
            # 濒危剪枝（每 100 代）
            gen: int = assessment["generation"]
            if gen > 0 and gen % 100 == 0:
                mutagen_scores: dict[str, float] = self._compute_mutagen_scores()
                self._mark_endangered(mutagen_scores, gen)
                self._prune_endangered(gen)
            
            strategy: dict[str, Any] = self._pick_mutation_strategy(assessment)
            self.apply_mutation(strategy)
            time.sleep(0.5)
        
        print(f"\n{'='*60}")
        print(f"Evolution complete")
        if self.history["mutations"]:
            status: str = (
                "success" if self.history["mutations"][-1]["result"] == "success"
                else "failed"
            )
        else:
            status = "no_mutations"
        print(f"   Last: {status}")
        print(f"   Generation: {self.history['generation']}")
        print(f"{'='*60}")
        return self.history


def main() -> None:
    import argparse
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="TORK Evolution Engine v3.0"
    )
    parser.add_argument("--rounds", "-r", type=int, default=1)
    parser.add_argument("--loop", "-l", action="store_true")
    parser.add_argument("--interval", "-i", type=int, default=600)
    parser.add_argument("--validate", "-v", action="store_true",
                        help="执行编译验证但不变异")
    args: argparse.Namespace = parser.parse_args()
    
    if args.validate:
        from cloud.validator import ValidatorPipeline
        pipe: ValidatorPipeline = ValidatorPipeline()
        result: dict[str, Any] = pipe.validate(runtime=False)
        print(json.dumps(result, indent=2))
        sys.exit(0 if result["compile_ok"] else 1)
    
    evo: TorkEvolution = TorkEvolution()
    if args.loop:
        print("Continuous evolution mode (v3.0)")
        while True:
            evo.evolve(rounds=args.rounds)
            print(f"\nSleeping {args.interval}s...\n")
            try:
                time.sleep(args.interval)
            except KeyboardInterrupt:
                print("Stopped")
                break
    else:
        evo.evolve(rounds=args.rounds)


if __name__ == "__main__":
    main()
