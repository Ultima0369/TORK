#!/usr/bin/env python3
"""TORK 真实参数进化引擎 — 直接调制 instinct.c 的参数，不是加注释。"""

import os, sys, re, json, subprocess, time, random
from datetime import datetime

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG_FILE = os.path.join(PROJECT, 'persist', 'real_evolution.json')
INSTINCT_SRC = os.path.join(PROJECT, 'instinct', 'instinct.c')

# 可调参数及其调制范围
TUNABLE_PARAMS = {
    'curiosity_weight':  (50, 200, 15,   '好奇心权重，越高越爱探索'),
    'fear_weight':       (50, 200, 15,   '恐惧权重，越高越保守'),
    'desire_weight':     (50, 200, 15,   '欲望权重，越高越进取'),
    'conservative_cycle':(10, 100, 5,    '保守模式周期长度'),
    'aggressive_cycle':  (20, 200, 10,   '激进模式周期长度'),
}

def read_param(param):
    """从 instinct.c 读取当前参数值。"""
    with open(INSTINCT_SRC, 'r') as f:
        content = f.read()
    pattern = rf'\.{re.escape(param)}\s*=\s*(\d+)'
    m = re.search(pattern, content)
    if m:
        return int(m.group(1))
    return None

def write_param(param, value):
    """写入参数值到 instinct.c。"""
    with open(INSTINCT_SRC, 'r') as f:
        content = f.read()
    pattern = rf'(\.{re.escape(param)}\s*=\s*)(\d+)'
    def replacer(m):
        return f'{m.group(1)}{value}'
    new_content = re.sub(pattern, replacer, content, count=1)
    if new_content != content:
        with open(INSTINCT_SRC, 'w') as f:
            f.write(new_content)
        return True
    return False

def build_and_smoke():
    """编译并冒烟测试。返回 (bool, str)。"""
    r = subprocess.run(['make', '-C', PROJECT, 'all'], 
                       capture_output=True, text=True, timeout=30)
    if r.returncode != 0:
        errors = [l for l in r.stderr.split('\n') if 'error:' in l.lower()][:3]
        return False, '; '.join(errors) if errors else 'compile failed'
    # 冒烟测试：启动引擎 3 秒
    try:
        proc = subprocess.Popen(['./build/tork_engine'], 
                                cwd=PROJECT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(3)
        proc.terminate()
        proc.wait(timeout=5)
        return True, 'smoke OK'
    except Exception as e:
        return False, str(e)
    return True, 'build OK'

def backup():
    """备份 instinct.c"""
    import shutil
    shutil.copy2(INSTINCT_SRC, INSTINCT_SRC + '.bak')

def restore():
    """恢复 instinct.c"""
    import shutil
    bak = INSTINCT_SRC + '.bak'
    if os.path.exists(bak):
        shutil.copy2(bak, INSTINCT_SRC)
        os.unlink(bak)

def load_log():
    if os.path.exists(LOG_FILE):
        with open(LOG_FILE) as f:
            return json.load(f)
    return {'evolutions': [], 'successes': 0, 'failures': 0}

def save_log(log):
    os.makedirs(os.path.dirname(LOG_FILE), exist_ok=True)
    with open(LOG_FILE, 'w') as f:
        json.dump(log, f, indent=2, ensure_ascii=False)

def evolve_one():
    """一次完整的进化循环。"""
    log = load_log()
    gen = len(log['evolutions']) + 1
    
    # 选择一个随机参数和随机方向
    param = random.choice(list(TUNABLE_PARAMS.keys()))
    min_v, max_v, step, desc = TUNABLE_PARAMS[param]
    direction = random.choice([-1, 1])
    delta = step * direction
    
    current = read_param(param)
    if current is None:
        print(f'  ⚠️  找不到参数 {param}')
        return False
    
    new_val = max(min_v, min(max_v, current + delta))
    if new_val == current:
        print(f'  ⏭   {param} 已在边界 ({current})')
        return False
    
    print(f'\n🧬 进化 #{gen}: {param} {current} → {new_val} ({desc})')
    print(f'   方向: {"↑ 增强" if direction > 0 else "↓ 减弱"}')
    
    # 备份
    backup()
    
    # 写入参数
    if not write_param(param, new_val):
        print(f'  ❌ 写入失败')
        restore()
        return False
    print(f'  ✓ 参数已写入')
    
    # 编译测试
    print(f'  🔨 编译中...')
    ok, msg = build_and_smoke()
    
    if ok:
        print(f'  ✅ 进化成功！{param}={new_val}')
        record = {
            'gen': gen,
            'timestamp': datetime.now().isoformat(),
            'param': param,
            'old_val': current,
            'new_val': new_val,
            'result': 'success',
            'message': msg,
        }
        log['evolutions'].append(record)
        log['successes'] += 1
        save_log(log)
        
        # Git 提交
        subprocess.run(['git', '-C', PROJECT, 'add', '-A'], capture_output=True)
        subprocess.run(['git', '-C', PROJECT, 'commit', 
                       '-m', f'auto: evolve {param} {current}→{new_val}'], 
                       capture_output=True)
        print(f'  📝 Git 已提交')
        return True
    else:
        print(f'  ❌ 进化失败: {msg}')
        print(f'  ↩️  回滚中...')
        restore()
        record = {
            'gen': gen,
            'timestamp': datetime.now().isoformat(),
            'param': param,
            'old_val': current,
            'new_val': new_val,
            'result': 'failure',
            'message': msg,
        }
        log['evolutions'].append(record)
        log['failures'] += 1
        save_log(log)
        return False

def main():
    print(f'\n{"="*60}')
    print(f'  🧬 TORK 真实进化引擎 — 参数调制模式')
    print(f'{"="*60}')
    
    log = load_log()
    gen = len(log['evolutions'])
    print(f'  已有进化: {gen} 次 ({log["successes"]} 成功, {log["failures"]} 失败)')
    
    # 显示当前参数
    print(f'\n  当前参数:')
    for param in TUNABLE_PARAMS:
        val = read_param(param)
        min_v, max_v, step, desc = TUNABLE_PARAMS[param]
        bar_len = 20
        pos = int((val - min_v) / (max_v - min_v) * bar_len) if max_v > min_v else bar_len // 2
        bar = '█' * pos + '░' * (bar_len - pos)
        print(f'    {param:25s} = {val:3d}  {bar}  {desc}')
    
    # 执行进化
    rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    for i in range(rounds):
        evolve_one()
    
    # 显示结果
    log = load_log()
    print(f'\n{"="*60}')
    print(f'  进化结果: {len(log["evolutions"])} 次总计')
    print(f'  ✅ {log["successes"]} 成功 | ❌ {log["failures"]} 失败')
    rate = log['successes'] / max(1, len(log['evolutions'])) * 100
    print(f'  成功率: {rate:.0f}%')
    print(f'{"="*60}\n')

if __name__ == '__main__':
    main()
