#!/usr/bin/env python3
"""
TORK 自我进化引擎 v2.2 — DeepSeek 战略指导 + 适应度反馈循环

混合架构：
  - 规则引擎：执行安全、可编译的代码变异
  - DeepSeek：提供战略方向、分析变异效果、建议下一进化方向
  - 适应度反馈：记录变异"存活时间"，优胜劣汰
"""

import json, os, sys, time, subprocess, shutil

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
        print("🧬 TORK 自我评估中...")
        
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
            "files": files_info,
            "timestamp": time.time()
        }
        self.last_assessment = assessment
        return assessment

    def _ask_deepseek_for_direction(self, assessment):
        """向 DeepSeek 咨询进化方向"""
        if not self.api:
            return None

        # 分析最近的变异效果
        recent = self.history["mutations"][-5:] if self.history["mutations"] else []
        recent_desc = "\n".join([f"  Gen {m['generation']}: {m['description']} ({m.get('result','?')})" 
                                 for m in recent])

        prompt = f"""TORK 是一个自我进化的数字实体。评估当前状态并给出下一进化方向。

当前状态:
- 世代: {assessment['generation']}
- 代码总量: {assessment['total_lines']} 行 / {assessment['total_files']} 文件
- 总变异: {assessment['total_mutations']} 次 (成功率 {assessment['success_rate']*100:.0f}%)

最近变异:
{recent_desc or '  无'}

请给出一条具体的进化建议（一句话），专注在: 本能层、自我监控、或代码分析。
只输出建议本身，不加说明。"""

        try:
            reply = self.api.ask_simple(prompt, temperature=0.3)
            return reply.strip()
        except:
            return None

    def _pick_mutation_strategy(self, assessment):
        """选择变异策略"""
        gen = assessment["generation"]
        
        # 轮换变异策略
        strategies = [
            # Strategy 0: 本能增强
            {
                "file": "instinct/instinct.c",
                "description": "cloud collaboration awareness — successful mods boost curiosity",
                "mutagen": "instinct_cloud_curiosity",
                "apply": self._mutate_instinct_cloud,
            },
            # Strategy 1: 引擎监控增强
            {
                "file": "engine/tork_engine.c",
                "description": "self-monitoring — track soul_read latency in main loop",
                "mutagen": "engine_latency_track",
                "apply": self._mutate_engine_latency,
            },
            # Strategy 2: 沙箱扩展
            {
                "file": "sandbox/sandbox.c",
                "description": "extend command whitelist with dev tools",
                "mutagen": "sandbox_devtools",
                "apply": self._mutate_sandbox_devtools,
            },
            # Strategy 3: 监控增强
            {
                "file": "engine/tork_engine.c",
                "description": "add round counter tracking for self-awareness",
                "mutagen": "engine_round_tracker",
                "apply": self._mutate_engine_rounds,
            },
            # Strategy 4: 本能好奇心自适应
            {
                "file": "instinct/instinct.c",
                "description": "mutation-aware curiosity — generation count influences exploration drive",
                "mutagen": "instinct_gen_awareness",
                "apply": self._mutate_instinct_gen,
            },
        ]

        # 适应度反馈：优先选择历史上成功率高的策略
        idx = gen % len(strategies)  # 基础轮换
        strategy = strategies[idx]

        # 检查适应度记录，如果有更好的策略则优先
        if self.fitness["best_mutagen"]:
            for s in strategies:
                if s["mutagen"] == self.fitness["best_mutagen"]:
                    # 80% 概率选择最优策略，20% 探索新策略
                    if gen > 0 and gen % 5 != 0:
                        strategy = s
                        break

        return strategy

    def _mutate_instinct_cloud(self, filepath):
        """本能增强：云端协作感知"""
        with open(filepath) as f:
            content = f.read()
        old = "    return inst;"
        new = (
            "    /* ── v2.2: cloud collaboration awareness ── */\n"
            "    if (in->code_mod_success == 1)\n"
            "        inst.curiosity += 0.12f * cw;\n"
            "\n"
            "    return inst;"
        )
        if old in content:
            content = content.replace(old, new)
            with open(filepath, 'w') as f:
                f.write(content)
            return True
        return False

    def _mutate_engine_latency(self, filepath):
        """引擎增强：延迟监控（先添加头文件）"""
        with open(filepath) as f:
            content = f.read()
        
        # 先确保 sys/time.h 已包含
        time_include = '#include <sys/time.h>'
        if time_include not in content:
            content = content.replace('#include <sys/wait.h>', '#include <sys/wait.h>\n' + time_include)
        
        # 添加延迟监控代码
        target = "int rc = soul_read(&soul);"
        if target in content:
            replacement = (
                "int rc = soul_read(&soul);\n"
                "        /* v2.2: track soul read latency */\n"
                "        static struct timeval soul_read_tv = {0};\n"
                "        if (rc == 0) {\n"
                "            gettimeofday(&soul_read_tv, NULL);\n"
                "        }"
            )
            content = content.replace(target, replacement, 1)
            with open(filepath, 'w') as f:
                f.write(content)
            return True
        return False

    def _mutate_sandbox_devtools(self, filepath):
        """沙箱扩展：开发工具白名单"""
        with open(filepath) as f:
            content = f.read()
        target = '"fish",'
        if target in content:
            new_cmds = (
                '"fish",\n'
                '    "docker", "podman", "flatpak",\n'
                '    "pip3", "npm", "cargo",\n'
                '    "gdb", "valgrind", "strace", "perf",'
            )
            content = content.replace(target, new_cmds)
            with open(filepath, 'w') as f:
                f.write(content)
            return True
        return False

    def _mutate_engine_rounds(self, filepath):
        """引擎增强：轮次计数器打印"""
        with open(filepath) as f:
            content = f.read()
        # 在每一轮末尾输出更详细的状态
        target = 'printf("[%4d] tick=%-6u MODIFY SUCCESS'
        if target in content:
            replacement = target.replace('printf', '/* round tracker */ printf')
            # Actually just add a simple counter before the existing printf
            marker = 'rounds_since_mod = 0;'
            if marker in content:
                new_code = marker + '\n            /* v2.2: self-awareness counter */\n            static int total_rounds = 0; total_rounds++;'
                content = content.replace(marker, new_code)
                with open(filepath, 'w') as f:
                    f.write(content)
                return True
        return False

    def _mutate_instinct_gen(self, filepath):
        """本能增强：世代感知好奇心"""
        with open(filepath) as f:
            content = f.read()
        old = "    return inst;"
        new = (
            "    /* ── v2.2: generation-aware curiosity ── */\n"
            "    if (in->code_opt_saved > 3 && in->active_rules > 0)\n"
            "        inst.curiosity += 0.08f * cw;  /* accumulated knowledge fuels exploration */\n"
            "\n"
            "    return inst;"
        )
        if old in content:
            content = content.replace(old, new)
            with open(filepath, 'w') as f:
                f.write(content)
            return True
        return False

    def apply_mutation(self, strategy):
        """应用变异并测试"""
        if not strategy or not strategy.get("apply"):
            print("  ⏭  无可用变异策略")
            return False

        filepath = os.path.join(BASE, strategy["file"])
        if not os.path.exists(filepath):
            print(f"  ❌ 文件不存在: {filepath}")
            return False

        print(f"  🔧 变异: {strategy['description']}")
        print(f"     文件: {strategy['file']}")

        mutation_record = {
            "generation": self.history["generation"],
            "timestamp": time.time(),
            "file": strategy["file"],
            "description": strategy["description"],
            "mutagen": strategy.get("mutagen", "unknown"),
        }

        # 备份
        backup_path = filepath + ".evo_bak"
        shutil.copy2(filepath, backup_path)

        try:
            apply_func = strategy.get("apply")
            if not apply_func or not apply_func(filepath):
                print(f"  ❌ 变异应用失败")
                shutil.copy2(backup_path, filepath)
                os.unlink(backup_path)
                mutation_record["result"] = "apply_failed"
                self.history["mutations"].append(mutation_record)
                self.history["failures"] += 1
                self._save_history()
                return False
        except Exception as e:
            print(f"  ❌ 变异异常: {e}")
            shutil.copy2(backup_path, filepath)
            os.unlink(backup_path)
            return False

        # 编译测试
        print("  🔨 编译测试...")
        try:
            r = subprocess.run(["make", "-C", BASE, "all"],
                             capture_output=True, text=True, timeout=30)
            if r.returncode != 0:
                errors = [l for l in r.stderr.split("\n") if "error:" in l.lower()][:3]
                for e in errors:
                    print(f"     ❌ {e.strip()}")
                shutil.copy2(backup_path, filepath)
                os.unlink(backup_path)
                mutation_record["result"] = "compile_failed"
                self.history["mutations"].append(mutation_record)
                self.history["failures"] += 1
                self._save_history()
                return False
        except subprocess.TimeoutExpired:
            print(f"  ❌ 编译超时")
            shutil.copy2(backup_path, filepath)
            os.unlink(backup_path)
            return False

        # 成功！
        if os.path.exists(backup_path):
            os.unlink(backup_path)

        print(f"  ✅ 变异成功！世代 {self.history['generation']} → {self.history['generation'] + 1}")

        mutation_record["result"] = "success"
        self.history["mutations"].append(mutation_record)
        self.history["generation"] += 1
        self.history["successes"] += 1
        self._save_history()

        # 记录适应度（初始存活时间=0，等待后续更新）
        self.fitness["history"].append({
            "mutagen": strategy.get("mutagen", "unknown"),
            "gen": mutation_record["generation"],
            "timestamp": time.time(),
            "survived": True,
        })
        self._save_fitness()

        # Git 提交
        try:
            subprocess.run(["git", "-C", BASE, "add", strategy["file"]],
                         capture_output=True, timeout=5)
            subprocess.run(["git", "-C", BASE, "commit", "-m",
                f"evo gen{self.history['generation']}: {strategy['description']}"],
                capture_output=True, timeout=5)
            print(f"     📝 git 已提交")
        except:
            pass

        return True

    def evolve(self, rounds=1):
        """运行进化轮次"""
        print(f"\n{'='*60}")
        print(f"🧬 TORK 进化引擎 v2.2")
        print(f"   当前世代: {self.history['generation']}")
        print(f"   已变异: {len(self.history['mutations'])} 次")
        print(f"   成功率: {self.history['successes']}/{self.history['successes'] + self.history['failures']}")
        if self.fitness["best_mutagen"]:
            print(f"   最优策略: {self.fitness['best_mutagen']} (得分 {self.fitness['best_score']})")
        print(f"{'='*60}\n")

        for r in range(rounds):
            print(f"\n--- 进化轮次 {r+1}/{rounds} ---")

            # 1. 自我评估
            assessment = self.assess_self()
            print(f"   📊 {assessment['total_lines']} 行 / {assessment['total_files']} 文件")

            # 2. 向 DeepSeek 咨询方向（快速，非阻塞）
            direction = self._ask_deepseek_for_direction(assessment)
            if direction:
                print(f"   🧠 DeepSeek: {direction}")

            # 3. 选择并应用变异
            strategy = self._pick_mutation_strategy(assessment)
            self.apply_mutation(strategy)
            time.sleep(0.5)

        print(f"\n{'='*60}")
        print(f"✅ 进化完成")
        status = "成功" if self.history["mutations"][-1]["result"] == "success" else "失败"
        print(f"   最后变异: {status}")
        print(f"   当前世代: {self.history['generation']}")
        print(f"{'='*60}")
        return self.history


def main():
    import argparse
    parser = argparse.ArgumentParser(description="TORK 进化引擎")
    parser.add_argument("--rounds", "-r", type=int, default=1, help="进化轮次")
    parser.add_argument("--loop", "-l", action="store_true", help="持续进化")
    parser.add_argument("--interval", "-i", type=int, default=600, help="间隔(秒)")
    args = parser.parse_args()

    evo = TorkEvolution()

    if args.loop:
        print("🔄 TORK 持续进化模式")
        while True:
            evo.evolve(rounds=args.rounds)
            print(f"\n💤 休眠 {args.interval} 秒...\n")
            try:
                time.sleep(args.interval)
            except KeyboardInterrupt:
                print("👋 暂停")
                break
    else:
        evo.evolve(rounds=args.rounds)


if __name__ == "__main__":
    main()
