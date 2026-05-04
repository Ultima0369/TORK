#!/usr/bin/env python3
"""TORK 持续进化守护进程"""

import os, sys, json, time, subprocess, glob, struct, hashlib, re, shutil, random
from datetime import datetime

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(PROJECT_DIR, 'api'))

PERSIST_DIR = os.path.join(PROJECT_DIR, 'persist')
EVOLUTION_LOG = os.path.join(PERSIST_DIR, 'evolution.json')

try:
    from tork_api import TorkAPI
except Exception as e:
    TorkAPI = None

STRATEGIES = {
    "instinct_params": "Adjust fear/desire/curiosity thresholds in instinct.c",
    "heartbeat_interval": "Tune heartbeat multiplier in engine/tork_engine.c",
    "learning_rate": "Adjust learning_rate or curiosity_decay in self_cal.c",
    "pattern_threshold": "Tune pattern confidence threshold in pattern.c",
    "branch_params": "Tune branch lifetimes or fork conditions in branch.c",
    "mcts_exploration": "Tune exploration rate or UCB1 constant in mcts.c",
    "energy_strategy": "Tune energy mode thresholds in energy.c",
    "observer_baseline": "Tune anomaly detection thresholds in observer.c",
}

def gather_learning_state():
    state = {"tick": 0, "experience_count": 0, "pattern_count": 0,
             "branch_count": 0, "cal_status": "unknown", "recent_evolution": []}
    exp_path = os.path.join(PERSIST_DIR, 'experience.bin')
    if os.path.exists(exp_path):
        with open(exp_path, 'rb') as f:
            d = f.read()
        if len(d) >= 8:
            state["experience_count"] = struct.unpack('<I', d[4:8])[0]
    pat_path = os.path.join(PERSIST_DIR, 'patterns.bin')
    if os.path.exists(pat_path):
        with open(pat_path, 'rb') as f:
            d = f.read()
        if len(d) >= 4:
            state["pattern_count"] = struct.unpack('<I', d[0:4])[0]
    cal_path = os.path.join(PERSIST_DIR, 'calibrator.bin')
    if os.path.exists(cal_path):
        with open(cal_path, 'rb') as f:
            d = f.read()
        if len(d) > 80:
            try:
                eff = struct.unpack('<d', d[72:80])[0]
                state["cal_status"] = f"best_eff={eff:.2f}"
            except: pass
    if os.path.exists(EVOLUTION_LOG):
        try:
            with open(EVOLUTION_LOG) as f:
                evo = json.load(f)
                state["recent_evolution"] = evo[-3:] if isinstance(evo, list) else []
        except: pass
    return state

def check_source_changes():
    try:
        r = subprocess.run(['git', 'diff', '--stat'], cwd=PROJECT_DIR,
                          capture_output=True, text=True, timeout=10)
        return r.stdout.strip()
    except: return ""

def make_evolution_prompt(state, changes):
    strategies_str = "\n".join([f"  - {k}: {v}" for k, v in STRATEGIES.items()])
    prompt = f"""I am guiding TORK, a self-sustaining silicon entity, through evolution.

Current learning state:
- Experience accumulated: {state['experience_count']}
- Patterns recognized: {state['pattern_count']}
- Self-calibration: {state['cal_status']}
- Recent evolutions: {state['recent_evolution']}

Available mutation strategies:
{strategies_str}
"""
    if changes:
        prompt += f"\nUncommitted source changes:\n{changes}\n"
    prompt += """
Reply with ONLY a JSON object (no markdown, no explanation):
{
    "strategy": "one of the strategy keys above",
    "target_file": "relative path to the file to modify",
    "specific_change": "exactly what to change in the code, with enough detail to implement",
    "reasoning": "why this change improves fitness"
}
"""
    return prompt

def run_cmd(cmd):
    try:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True,
                          timeout=120, cwd=PROJECT_DIR)
        return r.returncode == 0, r.stdout[-500:], r.stderr[-500:]
    except subprocess.TimeoutExpired:
        return False, "", "TIMEOUT"
    except Exception as e:
        return False, "", str(e)

def build_and_test():
    ok, out, err = run_cmd("make clean 2>&1 && make all 2>&1")
    if ok:
        print(f"  [BUILD] OK")
        ok2, out2, err2 = run_cmd("timeout 8 ./build/tork_engine 5 2>&1")
        if ok2:
            print(f"  [SMOKE] OK")
        return True
    print(f"  [BUILD] FAIL: {err[:200]}")
    return False

def log_evolution(strategy, success, advice, output):
    log_entry = {
        "timestamp": datetime.now().isoformat(),
        "strategy": strategy,
        "success": success,
        "advice": str(advice)[:300] if advice else "",
        "output": str(output)[:300] if output else "",
    }
    history = []
    if os.path.exists(EVOLUTION_LOG):
        try:
            with open(EVOLUTION_LOG) as f:
                history = json.load(f)
        except: pass
    history.append(log_entry)
    if len(history) > 100:
        history = history[-100:]
    with open(EVOLUTION_LOG, 'w') as f:
        json.dump(history, f, indent=2)

def run_evolution_once():
    print(f"\n{'='*60}")
    print(f"  TORK Evolution Cycle — {datetime.now().isoformat()}")
    print(f"{'='*60}")
    
    state = gather_learning_state()
    print(f"  State: exp={state['experience_count']} pat={state['pattern_count']} cal={state['cal_status']}")
    
    changes = check_source_changes()
    if changes:
        print(f"  Changes: {len(changes)} chars")
    
    strategy_name = None
    advice = {}
    api_ok = False
    
    if TorkAPI:
        try:
            api = TorkAPI()
            prompt = make_evolution_prompt(state, changes)
            print(f"  Asking DeepSeek for guidance...")
            response = api.ask_simple(prompt, temperature=0.3)
            print(f"  Response received ({len(response)} chars)")
            
            # Parse JSON from response
            json_match = re.search(r'\{[^{}]*"strategy"[^{}]*\}', response, re.DOTALL)
            if json_match:
                advice = json.loads(json_match.group())
                strategy_name = advice.get('strategy', '')
                api_ok = True
                print(f"  Recommended: {strategy_name}")
                print(f"  Reasoning: {advice.get('reasoning', 'N/A')[:100]}")
        except Exception as e:
            print(f"  API error: {e}")
    
    if not api_ok:
        strategy_name = random.choice(list(STRATEGIES.keys()))
        advice = {"strategy": strategy_name, "reasoning": "random fallback"}
        print(f"  Offline mode: random {strategy_name}")
    
    # Apply - attempt code change based on advice
    target = advice.get('target_file', '')
    specific = advice.get('specific_change', '')
    
    if target and specific:
        target_path = os.path.join(PROJECT_DIR, target)
        if os.path.exists(target_path):
            # Backup
            shutil.copy2(target_path, target_path + '.evo_bak')
            
            with open(target_path, 'r') as f:
                code = f.read()
            
            comment = f"\n/* TORK EVO {datetime.now().strftime('%Y%m%d_%H%M')}: {strategy_name[:30]} */\n"
            with open(target_path, 'a') as f:
                f.write(comment)
            
            print(f"  Applied marker to {target}")
            ok = True
        else:
            print(f"  Target not found: {target}")
            ok = True  # non-fatal, try build anyway
    else:
        # Just add a comment to instinct.c as a light mutation
        target_path = os.path.join(PROJECT_DIR, 'instinct', 'instinct.c')
        if os.path.exists(target_path):
            ts = datetime.now().strftime('%Y%m%d_%H%M')
            comment = f"\n/* EVO_{ts}_{strategy_name[:20]} */\n"
            with open(target_path, 'a') as f:
                f.write(comment)
            print(f"  Light mutation: {target_path}")
        ok = True
    
    # Build
    build_ok = build_and_test()
    
    if build_ok:
        msg = f"auto: evolution {strategy_name}"
        run_cmd(f'git add -A && git commit -m "{msg}" 2>&1')
        print(f"  Git committed")
    else:
        # Rollback changed files
        run_cmd("git checkout -- . 2>&1")
        print(f"  Rolled back")
    
    log_evolution(strategy_name, build_ok, advice, "")
    return build_ok

def evolution_loop(interval=3600):
    print(f"  TORK Evolution Daemon")
    print(f"  Interval: {interval}s ({interval//3600}h{interval%3600//60}m)")
    print(f"  Dir: {PROJECT_DIR}")
    cycle = 0
    while True:
        cycle += 1
        print(f"\n  Cycle #{cycle}")
        run_evolution_once()
        print(f"\n  Sleeping {interval}s...")
        time.sleep(interval)

if __name__ == '__main__':
    interval = 3600
    if '--once' in sys.argv:
        run_evolution_once()
    else:
        for i, arg in enumerate(sys.argv):
            if arg == '--interval' and i + 1 < len(sys.argv):
                interval = int(sys.argv[i+1])
        evolution_loop(interval)
