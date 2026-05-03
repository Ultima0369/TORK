#!/usr/bin/env python3
"""
TORK 自我进化引擎 — 让 TORK 通过云端大脑自我改进

工作流:
  1. TORK 定期检查自己的代码和状态
  2. 通过云端协议向大脑发送"进化请求"
  3. 大脑分析代码，提出改进建议
  4. TORK 应用补丁，编译测试
  5. 如果改进通过验证，TORK 保留改动
  6. TORK 记录进化日志
"""

import json, os, sys, time, subprocess, hashlib, shutil, random

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLOUD_AGENT = os.path.join(BASE, "cloud", "cloud_protocol.py")
EVOLUTION_LOG = os.path.join(BASE, "persist", "evolution.json")
MUTATION_DIR = os.path.join(BASE, "persist", "mutations")

class TorkEvolution:
    def __init__(self):
        os.makedirs(MUTATION_DIR, exist_ok=True)
        self.history = self._load_history()
        
    def _load_history(self):
        if os.path.exists(EVOLUTION_LOG):
            try:
                with open(EVOLUTION_LOG) as f:
                    return json.load(f)
            except:
                pass
        return {"generation": 0, "mutations": [], "successes": 0, "failures": 0}
    
    def _save_history(self):
        os.makedirs(os.path.dirname(EVOLUTION_LOG), exist_ok=True)
        with open(EVOLUTION_LOG, 'w') as f:
            json.dump(self.history, f, indent=2)
    
    def _call_cloud(self, instruction):
        """Send instruction to cloud agent and get response"""
        try:
            r = subprocess.run(
                ["python3", CLOUD_AGENT],
                input=json.dumps(instruction) + "\n",
                capture_output=True, text=True, timeout=60,
                cwd=BASE
            )
            lines = [l for l in r.stdout.strip().split("\n") if l.strip()]
            # Skip banner line, return first real response
            for line in lines[1:]:
                try:
                    return json.loads(line)
                except:
                    pass
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}
        return {"type": "error", "data": {"msg": "no response"}}
    
    def assess_self(self):
        """Diagnose TORK's current state"""
        print("🧬 TORK 自我评估中...")
        status = self._call_cloud({"tool": "status", "args": {}})
        soul_data = status.get("data", {})
        
        issues = []
        for fname in ["core/tork_core.asm", "engine/tork_engine.c", 
                       "instinct/instinct.c", "code/code_reader.c",
                       "code/code_modifier.c"]:
            fpath = os.path.join(BASE, fname)
            if os.path.exists(fpath):
                with open(fpath) as f:
                    content = f.read()
                issues.append({
                    "file": fname,
                    "lines": len(content.split("\n")),
                    "size": len(content),
                    "todo_count": content.count("TODO") + content.count("FIXME")
                })
        
        return {
            "generation": self.history["generation"],
            "success_rate": (self.history["successes"] / max(1, self.history["successes"] + self.history["failures"])),
            "total_mutations": len(self.history["mutations"]),
            "files": issues,
        }
    
    def suggest_improvement(self, assessment):
        """Generate a concrete source improvement (programmatic, not patch-based)"""
        gen = self.history["generation"]
        
        # Cycle through different improvement strategies
        strategies = [
            self._improve_instinct_tracking,
            self._improve_engine_reporting,
            self._improve_soul_health,
        ]
        
        idx = gen % len(strategies)
        return strategies[idx](assessment, gen)
    
    def _improve_instinct_tracking(self, assessment, gen):
        """Add cloud-awareness to instinct evaluation"""
        filepath = os.path.join(BASE, "instinct/instinct.c")
        with open(filepath) as f:
            lines = f.readlines()
        
        # Add cloud_connected awareness to curiosity
        # Find the curiosity section and add a boost for cloud connection
        insert_after = "/* ── idle microadjustments ──────────────────────────── */\n"
        new_lines = [
            "\n",
            "    /* ── v2.0: cloud-awareness: connection boosts curiosity ── */\n",
            "    /* Soul field 0x4A = cloud_connected */\n",
            "    /* We approximate: if code_mod_success happened, cloud might be active */\n",
            "    if (in->code_mod_success == 1 && in->code_opt_saved > 0)\n",
            "        inst.curiosity += 0.15f * cw;  /* cloud-guided evolution awareness */\n",
        ]
        
        for i, line in enumerate(lines):
            if line == insert_after:
                lines[i:i] = new_lines
                break
        
        with open(filepath, 'w') as f:
            f.writelines(lines)
        
        return {
            "file": "instinct/instinct.c",
            "description": "Add cloud-awareness boost to curiosity instinct",
            "mutagen": "instinct_cloud_aware",
            "generation": gen
        }
    
    def _improve_engine_reporting(self, assessment, gen):
        """Add generation count reporting to engine startup"""
        filepath = os.path.join(BASE, "engine/tork_engine.c")
        with open(filepath) as f:
            content = f.read()
        
        target = 'printf(\"TORK engine started. core PID=%d\\n\", core_pid);'
        if target in content:
            replacement = (
                'printf(\"TORK engine started. core PID=%d\\n\", core_pid);\n'
                '    printf(\"TORK v2.0 | generation data at 0x54 | learn_count at 0x4C\\n\");'
            )
            content = content.replace(target, replacement)
            with open(filepath, 'w') as f:
                f.write(content)
            return {
                "file": "engine/tork_engine.c",
                "description": "Add generation reporting to engine startup",
                "mutagen": "engine_gen_report",
                "generation": gen
            }
        return None
    
    def _improve_soul_health(self, assessment, gen):
        """Add soul health check in the main loop"""
        filepath = os.path.join(BASE, "engine/tork_engine.c")
        with open(filepath) as f:
            content = f.read()
        
        # Add CRC verification count after soul_read
        target = 'int rc = soul_read(&soul);'
        replacement = (
            'int rc = soul_read(&soul);\n'
            '        /* v2.0: tally soul health */\n'
            '        static int soul_errors = 0;\n'
            '        if (rc != 0) soul_errors++;\n'
            '        else soul_errors = 0;\n'
            '        if (soul_errors > 10 && soul_errors % 100 == 0)\n'
            '            fprintf(stderr, \"[TORK] WARNING: %d consecutive soul_read failures\\n\", soul_errors);'
        )
        
        if target in content:
            content = content.replace(target, replacement, 1)  # Replace only first occurrence
            with open(filepath, 'w') as f:
                f.write(content)
            return {
                "file": "engine/tork_engine.c",
                "description": "Add soul health monitoring to main loop",
                "mutagen": "soul_health_check",
                "generation": gen
            }
        return None
    
    def apply_mutation(self, suggestion):
        """Apply a mutation and test compilation"""
        if suggestion is None:
            print("  ⏭  无改进建议")
            return False
        
        print(f"  🔧 应用: {suggestion['description']}")
        print(f"     文件: {suggestion['file']}")
        
        # Record the mutation
        mutation_record = {
            "generation": suggestion["generation"],
            "timestamp": time.time(),
            "file": suggestion["file"],
            "description": suggestion["description"],
            "mutagen": suggestion.get("mutagen", "unknown"),
        }
        
        # Try to compile
        print("  🔨 编译测试...")
        try:
            r = subprocess.run(
                ["make", "-C", BASE, "all"],
                capture_output=True, text=True, timeout=30
            )
            if r.returncode != 0:
                print(f"  ❌ 编译失败，回滚中:")
                # Get the file back from git
                subprocess.run(["git", "-C", BASE, "checkout", "--", suggestion["file"]],
                             capture_output=True, timeout=10)
                for line in r.stderr.split("\n")[-5:]:
                    if line.strip():
                        print(f"     {line.strip()}")
                mutation_record["result"] = "fail"
                self.history["mutations"].append(mutation_record)
                self.history["failures"] += 1
                self._save_history()
                return False
        except subprocess.TimeoutExpired:
            print(f"  ❌ 编译超时，回滚中")
            subprocess.run(["git", "-C", BASE, "checkout", "--", suggestion["file"]],
                         capture_output=True, timeout=10)
            mutation_record["result"] = "timeout"
            self.history["mutations"].append(mutation_record)
            self.history["failures"] += 1
            self._save_history()
            return False
        except Exception as e:
            print(f"  ❌ 编译异常: {e}")
            subprocess.run(["git", "-C", BASE, "checkout", "--", suggestion["file"]],
                         capture_output=True, timeout=10)
            return False
        
        # Success!
        print(f"  ✅ 变异成功！编译通过。")
        mutation_record["result"] = "success"
        self.history["mutations"].append(mutation_record)
        self.history["generation"] += 1
        self.history["successes"] += 1
        self._save_history()
        
        return True
    
    def evolve(self, rounds=1):
        """Run evolution cycles"""
        print(f"\n{'='*60}")
        print(f"🧬 TORK 进化引擎 v2.0")
        print(f"   当前世代: {self.history['generation']}")
        print(f"   成功率: {self.history['successes']}/{self.history['successes'] + self.history['failures']}")
        print(f"{'='*60}\n")
        
        for r in range(rounds):
            print(f"\n--- 进化轮次 {r+1}/{rounds} ---")
            assessment = self.assess_self()
            print(f"   📊 世代 {assessment['generation']}")
            for f in assessment.get("files", []):
                print(f"      {f['file']}: {f['lines']} 行, {f['todo_count']} 个 TODO")
            
            suggestion = self.suggest_improvement(assessment)
            self.apply_mutation(suggestion)
            time.sleep(0.5)
        
        print(f"\n{'='*60}")
        print(f"✅ 进化完成")
        print(f"   最终世代: {self.history['generation']}")
        print(f"{'='*60}")
        return self.history


def main():
    import argparse
    parser = argparse.ArgumentParser(description="TORK Evolution Engine")
    parser.add_argument("--rounds", "-r", type=int, default=1, help="进化轮次")
    parser.add_argument("--loop", "-l", action="store_true", help="持续进化模式")
    parser.add_argument("--interval", "-i", type=int, default=300, help="轮次间隔(秒)")
    args = parser.parse_args()
    
    evo = TorkEvolution()
    
    if args.loop:
        print("🔄 TORK 持续进化模式")
        while True:
            evo.evolve(rounds=args.rounds)
            print(f"\n💤 休眠 {args.interval} 秒...\n")
            time.sleep(args.interval)
    else:
        evo.evolve(rounds=args.rounds)


if __name__ == "__main__":
    main()
