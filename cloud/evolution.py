#!/usr/bin/env python3
"""
TORK 自我进化引擎 v2.3 — DeepSeek 战略指导 + 适应度反馈循环

混合架构：
  - 规则引擎：执行安全、可编译的代码变异
  - DeepSeek：提供战略方向、分析变异效果、建议下一进化方向
  - 适应度反馈：记录变异"存活时间"，优胜劣汰
"""

import json, os, sys, time, subprocess, shutil, re

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
        except:
            self.api = None

    def _load_json(self, path, default):
        if os.path.exists(path):
            try:
                with open(path) as f:
                    return json.load(f)
            except:
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
            "core/tork_core.asm", "engine/tork_engine.c",
            "instinct/instinct.c", "instinct/instinct.h",
            "code/code_reader.c", "code/code_modifier.c",
            "code/code_modifier.h", "engine/soul_access.h",
            "engine/blackboard.c", "engine/calibrator.c",
            "engine/fission.c", "engine/idler.c",
            "engine/inductor.c", "engine/persistor.c",
            "engine/monitor.c", "sandbox/sandbox.c",
            "sandbox/sandbox.h", "install/agreement.c",
            "install/agreement.h", "cloud/cloud_protocol.py",
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
            except:
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
        except:
            return None

    def _pick_mutation_strategy(self, assessment):
        gen = assessment["generation"]
        strategies = [
            {"file": "instinct/instinct.c", "description": "cloud collaboration awareness", "mutagen": "instinct_cloud_curiosity", "apply": self._mutate_instinct_cloud},
            {"file": "engine/tork_engine.c", "description": "soul_read latency tracking", "mutagen": "engine_latency_track", "apply": self._mutate_engine_latency},
            {"file": "sandbox/sandbox.c", "description": "extend dev tools whitelist", "mutagen": "sandbox_devtools", "apply": self._mutate_sandbox_devtools},
            {"file": "engine/tork_engine.c", "description": "round counter self-awareness", "mutagen": "engine_round_tracker", "apply": self._mutate_engine_rounds},
            {"file": "instinct/instinct.c", "description": "generation-aware curiosity", "mutagen": "instinct_gen_awareness", "apply": self._mutate_instinct_gen},
            {"file": "instinct/instinct.c", "description": "curiosity_weight +15", "mutagen": "curiosity", "apply": self._mutate_instinct_curiosity_up},
            {"file": "instinct/instinct.c", "description": "fear_weight +15", "mutagen": "fear", "apply": self._mutate_instinct_fear_up},
            {"file": "instinct/instinct.c", "description": "desire_weight +15", "mutagen": "desire", "apply": self._mutate_instinct_desire_up},
            {"file": "instinct/instinct.c", "description": "conservative_cycle -5", "mutagen": "cycle", "apply": self._mutate_cycle_faster},
            {"file": "instinct/instinct.c", "description": "conservative_cycle +5", "mutagen": "cycle", "apply": self._mutate_cycle_slower},
        ]
        idx = gen % len(strategies)
        strategy = strategies[idx]
        if self.fitness["best_mutagen"]:
            for s in strategies:
                if s["mutagen"] == self.fitness["best_mutagen"]:
                    if gen > 0 and gen % 5 != 0:
                        strategy = s
                        break
        return strategy

    # ── Mutation implementations ──

    def _modulate_struct_value(self, filepath, field, delta):
        """Modulate a C struct initializer field by delta."""
        with open(filepath, 'r') as f:
            content = f.read()
        pattern = rf'(\.{re.escape(field)}\s*=\s*)(\d+)(,)'
        match = re.search(pattern, content)
        if not match:
            return False
        old_val = int(match.group(2))
        new_val = max(0, old_val + delta)
        content = re.sub(pattern, lambda m: m.group(1) + str(new_val) + m.group(3), content, count=1)
        with open(filepath, 'w') as f:
            f.write(content)
        print(f"  [EVO] {field}: {old_val} -> {new_val}")
        return True

    def _mutate_instinct_cloud(self, filepath):
        with open(filepath) as f:
            content = f.read()
        old = "    return inst;"
        new = "    /* v2.3: cloud collaboration awareness */\n    if (in->code_mod_success == 1)\n        inst.curiosity += 0.12f * cw;\n\n    return inst;"
        if old in content:
            content = content.replace(old, new, 1)
            with open(filepath, 'w') as f:
                f.write(content)
            return True
        return False

    def _mutate_engine_latency(self, filepath):
        with open(filepath) as f:
            content = f.read()
        time_inc = '#include <sys/time.h>'
        if time_inc not in content:
            content = content.replace('#include <sys/wait.h>', '#include <sys/wait.h>\n' + time_inc)
        target = "int rc = soul_read(&soul);"
        if target in content:
            replacement = "int rc = soul_read(&soul);\n        /* v2.3: soul read latency */\n        static struct timeval soul_read_tv = {0};\n        if (rc == 0) gettimeofday(&soul_read_tv, NULL);"
            content = content.replace(target, replacement, 1)
            with open(filepath, 'w') as f:
                f.write(content)
            return True
        return False

    def _mutate_sandbox_devtools(self, filepath):
        with open(filepath) as f:
            content = f.read()
        target = '"fish",'
        if target in content:
            content = content.replace(target, '"fish",\n    "docker", "podman", "flatpak",\n    "pip3", "npm", "cargo",\n    "gdb", "valgrind", "strace", "perf",')
            with open(filepath, 'w') as f:
                f.write(content)
            return True
        return False

    def _mutate_engine_rounds(self, filepath):
        with open(filepath) as f:
            content = f.read()
        marker = 'rounds_since_mod = 0;'
        if marker in content:
            new_code = marker + '\n            /* v2.3: self-awareness counter */ static int total_rounds = 0; total_rounds++;'
            content = content.replace(marker, new_code, 1)
            with open(filepath, 'w') as f:
                f.write(content)
            return True
        return False

    def _mutate_instinct_gen(self, filepath):
        with open(filepath) as f:
            content = f.read()
        old = "    return inst;"
        new = "    /* v2.3: generation-aware curiosity */\n    if (in->code_opt_saved > 3 && in->active_rules > 0)\n        inst.curiosity += 0.08f * cw;\n\n    return inst;"
        if old in content:
            content = content.replace(old, new, 1)
            with open(filepath, 'w') as f:
                f.write(content)
            return True
        return False

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
                self._save_history()
                return False
        except Exception as e:
            print(f"  Exception: {e}")
            shutil.copy2(backup_path, filepath)
            os.unlink(backup_path)
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
                self._save_history()
                return False
        except subprocess.TimeoutExpired:
            print(f"  Compile timeout")
            shutil.copy2(backup_path, filepath)
            os.unlink(backup_path)
            return False
        if os.path.exists(backup_path):
            os.unlink(backup_path)
        print(f"  Success! Gen {self.history['generation']} -> {self.history['generation'] + 1}")
        mutation_record["result"] = "success"
        self.history["mutations"].append(mutation_record)
        self.history["generation"] += 1
        self.history["successes"] += 1
        self._save_history()
        self.fitness["history"].append({
            "mutagen": strategy.get("mutagen", "unknown"),
            "gen": mutation_record["generation"],
            "timestamp": time.time(),
            "survived": True,
        })
        self._save_fitness()
        try:
            subprocess.run(["git", "-C", BASE, "add", strategy["file"]], capture_output=True, timeout=5)
            subprocess.run(["git", "-C", BASE, "commit", "-m",
                f"evo gen{self.history['generation']}: {strategy['description']}"],
                capture_output=True, timeout=5)
        except:
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
        status = "success" if self.history["mutations"][-1]["result"] == "success" else "failed"
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