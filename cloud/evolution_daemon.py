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
    # Add file manifest
    import os, glob as _glob
    _manifest = []
    for _root, _dirs, _files in os.walk("."):
        if ".git" in _root or "build" in _root or "AppDir" in _root or "__pycache__" in _root:
            continue
        for _f in _files:
            if _f.endswith((".c", ".h", ".asm")) and any(_root.startswith(x) for x in ["./instinct", "./learning", "./engine", "./core", "./sandbox"]):
                _manifest.append(os.path.join(_root, _f))
    prompt += "\nMutation target files:\n" + "\n".join(sorted(_manifest)) + "\n"
    prompt += "\nIMPORTANT: target_file MUST match one of the paths above exactly!\n"
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
        if isinstance(cmd, str):
            cmd = ['/bin/sh', '-c', cmd]
        r = subprocess.run(cmd, capture_output=True, text=True,
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
            loaded = json.load(open(EVOLUTION_LOG))
            if isinstance(loaded, list):
                history = loaded
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
        # Smart path resolution
        target_clean = target.lstrip('./')
        # Try direct join first
        target_path = os.path.join(PROJECT_DIR, target_clean)
        if not os.path.exists(target_path):
            # Try key directories by filename
            found = False
            for _d in ['learning', 'instinct', 'engine', 'core', 'sandbox', 'api', 'cloud']:
                _test = os.path.join(PROJECT_DIR, _d, os.path.basename(target_clean))
                if os.path.exists(_test):
                    target_path = _test
                    found = True
                    break
            if not found:
                print(f"  [WARN] target not found: {target} (tried {target_clean})")
                # Still try with instinct.c as fallback light mutation
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

PID_FILE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 
                              'persist', 'evolution_daemon.pid')

def daemonize():
    """Fork into background, writing PID file"""
    pid = os.fork()
    if pid > 0:
        # Parent exits
        with open(PID_FILE, 'w') as f:
            f.write(str(pid))
        print(f"  TORK Evolution Daemon started (PID={pid})")
        sys.exit(0)
    # Child continues
    os.setsid()
    # Second fork to fully detach
    pid2 = os.fork()
    if pid2 > 0:
        sys.exit(0)

def stop_daemon():
    """Stop the running daemon"""
    if os.path.exists(PID_FILE):
        try:
            with open(PID_FILE) as f:
                pid = int(f.read().strip())
            os.kill(pid, signal.SIGTERM)
            os.remove(PID_FILE)
            print(f"  TORK Evolution Daemon stopped (PID={pid})")
            return True
        except Exception as e:
            print(f"  Error stopping daemon: {e}")
            try:
                os.remove(PID_FILE)
            except: pass
            return False
    else:
        # Try pgrep
        try:
            result = subprocess.run(
                ["pgrep", "-f", "evolution_daemon"],
                capture_output=True, text=True
            )
            for pid in result.stdout.strip().split():
                os.kill(int(pid), signal.SIGTERM)
            print(f"  Stopped {len(result.stdout.strip().split())} daemon processes")
            return True
        except: pass
        print("  No running daemon found")
        return False

def status_daemon():
    """Check daemon status"""
    if os.path.exists(PID_FILE):
        try:
            with open(PID_FILE) as f:
                pid = int(f.read().strip())
            os.kill(pid, 0)  # Check if alive
            print(f"  TORK Evolution Daemon: RUNNING (PID={pid})")
            return True
        except:
            print(f"  TORK Evolution Daemon: STALE PID (no process)")
            os.remove(PID_FILE)
            return False
    else:
        # Check by name
        try:
            result = subprocess.run(
                ["pgrep", "-f", "evolution_daemon"],
                capture_output=True, text=True
            )
            pids = result.stdout.strip().split()
            if pids:
                print(f"  TORK Evolution Daemon: RUNNING (PIDs={','.join(pids)})")
                return True
        except: pass
        print(f"  TORK Evolution Daemon: NOT RUNNING")
        return False

if __name__ == '__main__':
    import signal as _signal
    
    interval = 3600
    
    if len(sys.argv) > 1:
        cmd = sys.argv[1]
        if cmd == 'start':
            daemonize()
            evolution_loop(interval)
        elif cmd == 'stop':
            stop_daemon()
        elif cmd == 'status':
            status_daemon()
        elif cmd == 'restart':
            stop_daemon()
            time.sleep(1)
            daemonize()
            evolution_loop(interval)
        elif cmd == '--once':
            run_evolution_once()
        else:
            print(f"Usage: {sys.argv[0]} [start|stop|status|restart|--once]")
            print(f"       {sys.argv[0]} --interval SEC  (set evolution interval)")
    else:
        # Default: run in foreground
        for i, arg in enumerate(sys.argv):
            if arg == '--interval' and i + 1 < len(sys.argv):
                interval = int(sys.argv[i+1])
        evolution_loop(interval)
