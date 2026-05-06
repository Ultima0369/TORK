#!/usr/bin/env python3
"""
TORK 自我进化引擎 v2.3 — DeepSeek 战略指导 + 适应度反馈循环

混合架构：
  - 规则引擎：执行安全、可编译的代码变异
  - DeepSeek：提供战略方向、分析变异效果、建议下一进化方向
  - 适应度反馈：记录变异"存活时间"，优胜劣汰
"""

import json, os, sys, time, subprocess, shutil, re, logging

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(BASE, 'api'))

EVOLUTION_LOG = os.path.join(BASE, "persist", "evolution.json")
MUTATION_DIR = os.path.join(BASE, "persist", "mutations")
FITNESS_LOG = os.path.join(BASE, "persist", "fitness.json")

class TorkEvolution:
    def __init__(self):
        os.makedirs(MUTATION_DIR, exist_ok=True)
        self.history = self._load_json(EVOLUTION_LOG,
            {"generation": 0, "mutations": [], "successes": 0, "failures": 0})
        self.fitness = self._load_json(FITNESS_LOG,
            {"history": [], "best_mutagen": None, "best_score": 0})
        self.last_assessment = None

        try:
            from tork_api import TorkAPI
            self.api = TorkAPI()
            self.api.timeout = 30
        except Exception:
            self.api = None

    def _load_json(self, path, default):
        if os.path.exists(path):
            try:
                with open(path) as f:
                    return json.load(f)
            except (json.JSONDecodeError, OSError):
                pass
        return default

    def _save_json(self, path, data):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w') as f:
            json.dump(data, f, indent=2)

    def _save_history(self):
        self._save_json(EVOLUTION_LOG, self.history)

    def _save_fitness(self):
        self._save_json(FITNESS_LOG, self.fitness)

    def assess_self(self):
        """自我评估"""
        print("TORK self-assessment...")

        source_files = [
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

        guide_rec = None
        guide_file = os.path.join(BASE, "persist", "mutation_guide.bin")
        if os.path.exists(guide_file):
            import struct
            try:
                with open(guide_file, 'rb') as gf:
                    data = gf.read()
                    if len(data) > 0x8F8:
                        guide_rec = {
                            'total_attempts': struct.unpack_from('I', data, 0x8F0)[0],
                            'total_successes': struct.unpack_from('I', data, 0x8F4)[0],
                        }
            except (json.JSONDecodeError, OSError):
                pass

        files_info = []
        total_lines = 0
        for fname in source_files:
            fpath = os.path.join(BASE, fname)
            if os.path.exists(fpath):
                with open(fpath) as f:
                    content = f.read()
                lines = len(content.split("\n"))
                files_info.append({"file": fname, "lines": lines})
                total_lines += lines

        assessment = {
            "generation": self.history["generation"],
            "total_lines": total_lines,
            "total_files": len(files_info),
            "success_rate": (self.history["successes"] / max(1, self.history["successes"] + self.history["failures"])),
            "total_mutations": len(self.history["mutations"]),
            "guide": guide_rec,
            "files": files_info,
            "timestamp": time.time()
        }
        self.last_assessment = assessment
        return assessment

    def _ask_deepseek_for_direction(self, assessment):
        if not self.api:
            return None
        recent = self.history["mutations"][-5:] if self.history["mutations"] else []
        recent_desc = "\n".join([f"  Gen {m['generation']}: {m['description']} ({m.get('result','?')})"
                                 for m in recent])
        prompt = f"""TORK is a self-evolving digital entity. Suggest next evolution direction.

Generation: {assessment['generation']}
Code: {assessment['total_lines']} lines / {assessment['total_files']} files
Mutations: {assessment['total_mutations']} (success rate {assessment['success_rate']*100:.0f}%)

Recent mutations:
{recent_desc or '  None'}

Give ONE concrete suggestion, focused on instinct/self-monitoring/code analysis. Output only the suggestion."""
        try:
            reply = self.api.ask_simple(prompt, temperature=0.3)
            return reply.strip()
        except Exception:
            return None

    def _pick_mutation_strategy(self, assessment):
        gen = assessment["generation"]
        strategies = [
            {"file": "src/instinct/instinct.c", "description": "cloud collaboration awareness", "mutagen": "instinct_cloud_curiosity", "apply": self._mutate_instinct_cloud},
            {"file": "src/engine/tork_engine.c", "description": "soul_read latency tracking", "mutagen": "engine_latency_track", "apply": self._mutate_engine_latency},
            {"file": "src/sandbox/sandbox.c", "description": "extend dev tools whitelist", "mutagen": "sandbox_devtools", "apply": self._mutate_sandbox_devtools},
            {"file": "src/engine/tork_engine.c", "description": "round counter self-awareness", "mutagen": "engine_round_tracker", "apply": self._mutate_engine_rounds},
            {"file": "src/instinct/instinct.c", "description": "generation-aware curiosity", "mutagen": "instinct_gen_awareness", "apply": self._mutate_instinct_gen},
            {"file": "src/instinct/instinct.c", "description": "curiosity_weight +15", "mutagen": "curiosity", "apply": self._mutate_instinct_curiosity_up},
            {"file": "src/instinct/instinct.c", "description": "fear_weight +15", "mutagen": "fear", "apply": self._mutate_instinct_fear_up},
            {"file": "src/instinct/instinct.c", "description": "desire_weight +15", "mutagen": "desire", "apply": self._mutate_instinct_desire_up},
            {"file": "src/instinct/instinct.c", "description": "conservative_cycle -5", "mutagen": "cycle", "apply": self._mutate_cycle_faster},
            {"file": "src/instinct/instinct.c", "description": "conservative_cycle +5", "mutagen": "cycle", "apply": self._mutate_cycle_slower},
        ]
        # 适应度加权选择：按历史存活分数选策略，而非轮询
        mutagen_scores = self._compute_mutagen_scores()

        # 过滤掉被冷冻的策略（frozen value > 当前 generation 表示仍在冻结期）
        gen = assessment["generation"]
        frozen = self.fitness.get("frozen", {})
        available = []
        for s in strategies:
            mid = s["mutagen"]
            if mid not in frozen or frozen[mid] <= gen:
                # 已过期或未冻结，从冻结列表中清除
                if mid in frozen and frozen[mid] <= gen:
                    del frozen[mid]
                available.append(s)

        if not available:
            # 所有策略都被冷冻，解冻最低分的
            available = strategies

        # 濒危剪枝：每100轮淘汰最低10%
        gen = assessment["generation"]
        if gen > 0 and gen % 100 == 0:
            self._mark_endangered(mutagen_scores, gen)
            self._prune_endangered(gen)

        import random
        if mutagen_scores:
            # Top 10% elite preservation: directly select the best strategy
            sorted_available = sorted(available, key=lambda s: mutagen_scores.get(s["mutagen"], 0), reverse=True)
            elite_cutoff = max(1, len(sorted_available) // 10)
            if random.random() < 0.1 and sorted_available:
                # 10% chance: pick from top 10% elite
                return random.choice(sorted_available[:elite_cutoff])
        # 90%: weighted random selection proportional to fitness
        weights = [max(mutagen_scores.get(s["mutagen"], 1.0), 0.01) for s in available]
        total = sum(weights)
        r = random.uniform(0, total)
        cumulative = 0.0
        for i, w in enumerate(weights):
            cumulative += w
            if r <= cumulative:
                return available[i]
        return available[gen % len(available)]

    def _compute_mutagen_scores(self):
        """适应度公式: (存活心跳数 × 0.6 + 语法正确(0/1) × 0.4) × 衰减因子
        衰减因子 = fitness 字段（由 _apply_fitness_decay 维护的半衰期指数衰减）
        这是代码世界唯一承认的"好" """
        scores = {}
        for entry in self.fitness["history"]:
            m = entry.get("mutagen", "unknown")
            survived = entry.get("survived", False)
            survival_ticks = entry.get("survival_ticks", 0)
            compile_ok = entry.get("compile_ok", 1 if survived else 0)
            decay = entry.get("fitness", 1.0)  # 半衰期衰减因子
            if m not in scores:
                scores[m] = 0.0
            # fitness = (survival_ticks × 0.6 + compile_ok × 0.4) × decay
            score = (survival_ticks * 0.6 + compile_ok * 0.4) * decay
            if survived:
                scores[m] += score
            else:
                scores[m] -= 0.5 * decay
        return scores

    def _mark_endangered(self, scores, gen):
        """每100轮：得分最低的10%策略标记为濒危（持久化到 fitness.json）"""
        if "endangered" not in self.fitness:
            self.fitness["endangered"] = {}
        if not scores:
            return
        sorted_mutagens = sorted(scores.items(), key=lambda x: x[1])
        cutoff = max(1, len(sorted_mutagens) // 10)
        for m, s in sorted_mutagens[:cutoff]:
            self.fitness["endangered"][m] = gen
        self._save_fitness()

    def _prune_endangered(self, gen):
        """200轮后濒危策略未改善则彻底移除：删除归档条目和代码文件"""
        if "endangered" not in self.fitness:
            return
        to_remove = []
        for m, mark_gen in list(self.fitness["endangered"].items()):
            if gen - mark_gen >= 200:
                # 检查是否改善
                scores = self._compute_mutagen_scores()
                if scores.get(m, 0) <= 0:
                    to_remove.append(m)
        for m in to_remove:
            del self.fitness["endangered"][m]
            # 移除该 mutagen 的 fitness 历史条目
            self.fitness["history"] = [
                e for e in self.fitness["history"] if e.get("mutagen") != m
            ]
            # 删除该 mutagen 的归档 diff 文件
            for fname in os.listdir(MUTATION_DIR):
                if m in fname:
                    try:
                        os.unlink(os.path.join(MUTATION_DIR, fname))
                    except OSError:
                        pass
            print(f"  [EVO] Pruned extinct strategy: {m}")
        if to_remove:
            self._save_fitness()

    def _apply_fitness_decay(self, mutagen, factor):
        """半衰期指数衰减: fitness = fitness × (0.5 ^ (ticks_since_last_success / half_life))
        factor 参数在此处作为 half_life 的语义：factor=0.5 表示半衰期=1次失败
        连续失败越多，衰减越剧烈——不是一次性砍半，而是指数坍缩"""
        HALF_LIFE = 3  # 每3次连续失败，权重减半
        for entry in self.fitness["history"]:
            if entry.get("mutagen") == mutagen and not entry.get("compile_ok", True):
                # 计算该条目以来该 mutagen 的连续失败次数
                consecutive_fails = 0
                for e in reversed(self.fitness["history"]):
                    if e.get("mutagen") == mutagen:
                        if not e.get("compile_ok", True):
                            consecutive_fails += 1
                        else:
                            break
                    else:
                        break
                # 指数衰减: 0.5^(fails/half_life)
                decay = 0.5 ** (consecutive_fails / HALF_LIFE)
                entry["fitness"] = entry.get("fitness", 1.0) * decay
        self._save_fitness()

    def _check_freeze_strategy(self, mutagen, gen):
        """连续3次编译失败 → 冷冻100轮"""
        if "frozen" not in self.fitness:
            self.fitness["frozen"] = {}
        # 计算该 mutagen 的连续失败次数
        fail_streak = 0
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
            print(f"  [EVO] Strategy {mutagen} frozen for 100 rounds (3 consecutive failures)")

    # ── TORK_EVOLVE marker injection ──

    def inject_at_marker(self, filepath, marker_name, protocol, new_code):
        """Inject code at a TORK_EVOLVE marker in a C source file.

        Protocols:
          INSERT_BEFORE — insert new_code on the line before the marker
          INSERT_AFTER  — insert new_code on the line after the marker
          REPLACE_LINE  — replace the marker line with new_code
          MODIFY_VALUE  — modify a numeric value on the marker line

        Double-injection guard: checks if the previous/next line already
        contains the injection signature (evo_injected comment).
        """
        with open(filepath, 'r') as f:
            lines = f.readlines()

        marker_line = None
        for i, line in enumerate(lines):
            if f'TORK_EVOLVE: {marker_name}' in line:
                marker_line = i
                break

        if marker_line is None:
            print(f"  [EVO] Marker '{marker_name}' not found in {filepath}")
            return False

        # Double-injection guard
        guard_sig = '/* evo_injected */'
        if protocol in ('INSERT_BEFORE', 'INSERT_AFTER'):
            check_idx = marker_line - 1 if protocol == 'INSERT_BEFORE' else marker_line + 1
            if 0 <= check_idx < len(lines) and guard_sig in lines[check_idx]:
                print(f"  [EVO] Marker '{marker_name}' already has injection — skipping")
                return False

        if protocol == 'INSERT_BEFORE':
            lines.insert(marker_line, new_code + '\n')
        elif protocol == 'INSERT_AFTER':
            lines.insert(marker_line + 1, new_code + '\n')
        elif protocol == 'REPLACE_LINE':
            lines[marker_line] = new_code + '\n'
        elif protocol == 'MODIFY_VALUE':
            # Find numeric value on marker line and modify it
            nums = re.findall(r'[\d.]+', lines[marker_line])
            if not nums:
                print(f"  [EVO] No numeric value found on marker line")
                return False
            old_val = float(nums[-1])
            new_val = old_val + float(new_code)
            lines[marker_line] = re.sub(r'[\d.]+$', str(int(new_val)), lines[marker_line])
        else:
            print(f"  [EVO] Unknown protocol: {protocol}")
            return False

        with open(filepath, 'w') as f:
            f.writelines(lines)
        print(f"  [EVO] Injected at '{marker_name}' ({protocol})")
        return True

    def _mutate_instinct_cloud(self, filepath):
        return self.inject_at_marker(filepath, 'instinct_return_before', 'INSERT_BEFORE',
            '    /* evo_injected */ if (in->code_mod_success == 1) inst.curiosity += 0.12f * cw;')

    def _mutate_engine_latency(self, filepath):
        # Uses engine_include_insert for the #include, then soul_read line
        rc1 = self.inject_at_marker(filepath, 'engine_include_insert', 'INSERT_AFTER',
            '#include <sys/time.h> /* evo_injected */')
        # The soul_read line is in main loop, no marker — use string fallback
        if rc1:
            with open(filepath, 'r') as f:
                content = f.read()
            target = "int rc = soul_read(&soul);"
            if target in content and 'soul_read_tv' not in content:
                replacement = target + '\n        /* evo_injected */ static struct timeval soul_read_tv = {0}; if (rc == 0) gettimeofday(&soul_read_tv, NULL);'
                content = content.replace(target, replacement, 1)
                with open(filepath, 'w') as f:
                    f.write(content)
                return True
        return rc1

    def _mutate_sandbox_devtools(self, filepath):
        return self.inject_at_marker(filepath, 'sandbox_devtools_insert', 'INSERT_AFTER',
            '    "docker", "podman", "flatpak", "pip3", "npm", "cargo", "gdb", "valgrind", "strace", "perf", /* evo_injected */')

    def _mutate_engine_rounds(self, filepath):
        return self.inject_at_marker(filepath, 'engine_rounds_insert', 'INSERT_BEFORE',
            '            /* evo_injected */ static int total_rounds = 0; total_rounds++;')

    def _mutate_instinct_gen(self, filepath):
        return self.inject_at_marker(filepath, 'instinct_return_before', 'INSERT_BEFORE',
            '    /* evo_injected */ if (in->code_opt_saved > 3 && in->active_rules > 0) inst.curiosity += 0.08f * cw;')

    def _mutate_instinct_curiosity_up(self, filepath):
        return self._modulate_struct_value(filepath, "curiosity_weight", 15)

    def _mutate_instinct_fear_up(self, filepath):
        return self._modulate_struct_value(filepath, "fear_weight", 15)

    def _mutate_instinct_desire_up(self, filepath):
        return self._modulate_struct_value(filepath, "desire_weight", 15)

    def _mutate_cycle_faster(self, filepath):
        return self._modulate_struct_value(filepath, "conservative_cycle", -5)

    def _mutate_cycle_slower(self, filepath):
        return self._modulate_struct_value(filepath, "conservative_cycle", 5)

    def modulate_param(self, target_file, param_name, new_value):
        """Modify a numeric parameter in source code."""
        full_path = os.path.join(BASE, target_file.lstrip('./'))
        if not os.path.exists(full_path):
            print(f"  [EVO] File not found: {full_path}")
            return False
        with open(full_path, 'r') as f:
            content = f.read()
        patterns = [
            rf'#define\s+{param_name}\s+([\d.]+)',
            rf'#define\s+{param_name}\s*\(([\d.]+)\)',
            rf'static\s+(int|float|double)\s+{param_name}\s*=\s*([\d.]+)',
            rf'(int|float|double)\s+{param_name}\s*=\s*([\d.]+)',
        ]
        for pattern in patterns:
            match = re.search(pattern, content)
            if match:
                old_val = match.group(1) if len(match.groups()) == 1 else match.group(2)
                old_str = match.group(0)
                new_str = old_str.replace(old_val, str(new_value))
                content = content.replace(old_str, new_str, 1)
                with open(full_path, 'w') as f:
                    f.write(content)
                print(f"  [EVO] {param_name}: {old_val} -> {new_value} in {target_file}")
                return True
        if 'instinct' in target_file:
            lines = content.split('\n')
            for i, line in enumerate(lines):
                if param_name.lower() in line.lower() and '=' in line and ('float' in line or 'int' in line or '#' in line):
                    nums = re.findall(r'[\d.]+', line)
                    if nums:
                        old_val = nums[-1]
                        lines[i] = line.replace(old_val, str(new_value))
                        with open(full_path, 'w') as f:
                            f.write('\n'.join(lines))
                        print(f"  [EVO] Line-mod {param_name} -> {new_value}")
                        return True
        print(f"  [EVO] {param_name} not found in {target_file}")
        return False

    def apply_mutation(self, strategy):
        if not strategy or not strategy.get("apply"):
            print("  No applicable strategy")
            return False
        filepath = os.path.join(BASE, strategy["file"])
        if not os.path.exists(filepath):
            print(f"  File not found: {filepath}")
            return False
        print(f"  Mutation: {strategy['description']}")
        print(f"     File: {strategy['file']}")
        mutation_record = {
            "generation": self.history["generation"],
            "timestamp": time.time(),
            "file": strategy["file"],
            "description": strategy["description"],
            "mutagen": strategy.get("mutagen", "unknown"),
        }
        backup_path = filepath + ".evo_bak"
        shutil.copy2(filepath, backup_path)
        try:
            apply_func = strategy.get("apply")
            if not apply_func or not apply_func(filepath):
                print(f"  Apply failed")
                shutil.copy2(backup_path, filepath)
                os.unlink(backup_path)
                mutation_record["result"] = "apply_failed"
                self.history["mutations"].append(mutation_record)
                self.history["failures"] += 1
                self.fitness["history"].append({
                    "mutagen": strategy.get("mutagen", "unknown"),
                    "gen": mutation_record["generation"],
                    "timestamp": time.time(),
                    "survived": False,
                    "survival_ticks": 0,
                })
                self._save_fitness()
                self._save_history()
                return False
        except Exception as e:
            print(f"  Exception: {e}")
            shutil.copy2(backup_path, filepath)
            os.unlink(backup_path)
            mutation_record["result"] = f"exception: {e}"
            self.history["mutations"].append(mutation_record)
            self.history["failures"] += 1
            self.fitness["history"].append({
                "mutagen": strategy.get("mutagen", "unknown"),
                "gen": mutation_record["generation"],
                "timestamp": time.time(),
                "survived": False,
                "survival_ticks": 0,
                "compile_ok": 0,
            })
            self._save_fitness()
            self._save_history()

            self._apply_fitness_decay(strategy.get("mutagen", "unknown"), 0.5)
            return False
        print("  Compile test...")
        try:
            r = subprocess.run(["make", "-C", BASE, "all"],
                             capture_output=True, text=True, timeout=30)
            if r.returncode != 0:
                errors = [l for l in r.stderr.split("\n") if "error:" in l.lower()][:3]
                for e in errors:
                    print(f"     {e.strip()}")
                shutil.copy2(backup_path, filepath)
                os.unlink(backup_path)
                mutation_record["result"] = "compile_failed"
                self.history["mutations"].append(mutation_record)
                self.history["failures"] += 1
                mutagen = strategy.get("mutagen", "unknown")
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
                self._save_fitness()
                self._save_history()
                return False
        except subprocess.TimeoutExpired:
            print(f"  Compile timeout")
            shutil.copy2(backup_path, filepath)
            os.unlink(backup_path)
            mutagen = strategy.get("mutagen", "unknown")
            mutation_record["result"] = "compile_timeout"
            self.history["mutations"].append(mutation_record)
            self.history["failures"] += 1
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
            self._save_fitness()
            self._save_history()
            return False
        if os.path.exists(backup_path):
            os.unlink(backup_path)
        print(f"  Success! Gen {self.history['generation']} -> {self.history['generation'] + 1}")
        mutation_record["result"] = "success"
        self.history["mutations"].append(mutation_record)
        self.history["generation"] += 1
        self.history["successes"] += 1
        self._save_history()

        # 计算存活分数：基于该 mutagen 历史成功次数和存活时间
        mutagen = strategy.get("mutagen", "unknown")
        # survival_ticks = 自上次同 mutagen 成功以来经过的世代数（真实存活时长）
        last_success_gen = -1
        for entry in reversed(self.fitness["history"]):
            if entry.get("mutagen") == mutagen and entry.get("survived"):
                last_success_gen = entry.get("gen", 0)
                break
        survival_ticks = max(1, self.history["generation"] - last_success_gen) if last_success_gen >= 0 else 1
        score = 1.0 + survival_ticks * 0.01

        self.fitness["history"].append({
            "mutagen": mutagen,
            "gen": mutation_record["generation"],
            "timestamp": time.time(),
            "survived": True,
            "survival_ticks": survival_ticks,
            "compile_ok": 1,
            "fitness": score,
        })
        # 更新 best_score 和 best_mutagen
        if score > self.fitness["best_score"]:
            self.fitness["best_score"] = score
            self.fitness["best_mutagen"] = mutagen
        self._save_fitness()
        try:
            subprocess.run(["git", "-C", BASE, "add", strategy["file"]], capture_output=True, timeout=5)
            subprocess.run(["git", "-C", BASE, "commit", "-m",
                f"evo gen{self.history['generation']}: {strategy['description']}"],
                capture_output=True, timeout=5)
        except Exception:
            pass
        return True

    def evolve(self, rounds=1):
        print(f"\n{'='*60}")
        print(f"TORK Evolution Engine v2.3")
        print(f"   Generation: {self.history['generation']}")
        print(f"   Mutations: {len(self.history['mutations'])}")
        print(f"   Success: {self.history['successes']}/{self.history['successes'] + self.history['failures']}")
        if self.fitness["best_mutagen"]:
            print(f"   Best: {self.fitness['best_mutagen']} (score {self.fitness['best_score']})")
        print(f"{'='*60}\n")
        for r in range(rounds):
            print(f"\n--- Round {r+1}/{rounds} ---")
            assessment = self.assess_self()
            print(f"   {assessment['total_lines']} lines / {assessment['total_files']} files")
            direction = self._ask_deepseek_for_direction(assessment)
            if direction:
                print(f"   DeepSeek: {direction}")
            strategy = self._pick_mutation_strategy(assessment)
            self.apply_mutation(strategy)
            time.sleep(0.5)
        print(f"\n{'='*60}")
        print(f"Evolution complete")
        if self.history["mutations"]:
            status = "success" if self.history["mutations"][-1]["result"] == "success" else "failed"
        else:
            status = "no_mutations"
        print(f"   Last: {status}")
        print(f"   Generation: {self.history['generation']}")
        print(f"{'='*60}")
        return self.history


def main():
    import argparse
    parser = argparse.ArgumentParser(description="TORK Evolution Engine")
    parser.add_argument("--rounds", "-r", type=int, default=1)
    parser.add_argument("--loop", "-l", action="store_true")
    parser.add_argument("--interval", "-i", type=int, default=600)
    args = parser.parse_args()
    evo = TorkEvolution()
    if args.loop:
        print("Continuous evolution mode")
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