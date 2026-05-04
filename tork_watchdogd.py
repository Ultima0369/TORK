#!/usr/bin/env python3
"""TORK Watchdog Daemon — 看门狗：发现高消耗进程，问老板"""
import os, time, json, subprocess, signal, sys
from pathlib import Path

CONFIG_DIR = Path.home() / ".config" / "tork"
KNOWN_FILE = CONFIG_DIR / "watchdog_known.json"
ALERT_FILE = Path("/tmp/tork_watchdog.alert")
FLAG_FILE  = Path("/tmp/tork.flag")
PID_FILE   = Path("/tmp/tork_watchdogd.pid")

# 阈值
CPU_THRESHOLD = 20.0   # CPU 超过 20%
MEM_THRESHOLD = 200.0  # MEM 超过 200MB
POLL_INTERVAL = 5      # 每 5 秒扫一次

# 系统进程白名单（默认不弹窗）
SYSTEM_PROCS = {
    'systemd', 'kthreadd', 'rcu_gp', 'rcu_par_gp', 'kworker', 'mm_percpu',
    'ksoftirqd', 'migration', 'cpuhp', 'kdevtmpfs', 'netns', 'kauditd',
    'khungtaskd', 'oom_reaper', 'writeback', 'kcompactd', 'ksmd', 'khugepaged',
    'watchdogd', 'watchdog/0', 'edac-poller', 'nvme', 'scsi_eh', 'scsi_tmf',
    'kworker/u256', 'irq', 'kernfs', 'kernfs_cache', 'cryptd', 'kadapt',
    'Xorg', 'wayland', 'gnome-shell', 'plasmashell', 'kwin', 'xfce',
    'systemd-journald', 'systemd-logind', 'systemd-udevd', 'systemd-resolved',
    'systemd-timesyncd', 'systemd-networkd', 'systemd-oomd',
    'dbus-daemon', 'dbus-broker', 'polkitd', 'rtkit-daemon',
    'NetworkManager', 'ModemManager', 'haveged', 'udisksd', 'upowerd',
    'gvfsd', 'gvfs-helper', 'goa-daemon', 'gsd-', 'pulseaudio',
    'pipewire', 'wireplumber', 'xdg-desktop', 'at-spi-bus',
}

def load_known():
    if KNOWN_FILE.exists():
        with open(KNOWN_FILE) as f:
            return json.load(f)
    return {}

def save_known(known):
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    with open(KNOWN_FILE, 'w') as f:
        json.dump(known, f, indent=2)

def get_proc_list():
    """返回 [{pid, name, cpu, mem_mb, cmdline}, ...]"""
    procs = []
    try:
        out = subprocess.run(
            ['ps', '--no-headers', '-eo', 'pid,comm,%cpu,rssize,args'],
            capture_output=True, text=True, timeout=5
        )
        for line in out.stdout.strip().split('\n'):
            if not line.strip():
                continue
            parts = line.strip().split(None, 4)
            if len(parts) < 4:
                continue
            pid_str, name, cpu_str, mem_str = parts[:4]
            cmdline = parts[4] if len(parts) > 4 else name
            try:
                pid = int(pid_str)
                cpu = float(cpu_str)
                mem_mb = float(mem_str) / 1024.0
                procs.append({'pid': pid, 'name': name, 'cpu': cpu, 'mem_mb': mem_mb, 'cmdline': cmdline})
            except (ValueError, IndexError):
                continue
    except Exception as e:
        print(f"  WATCHDOGD: ps failed: {e}", file=sys.stderr)
    return procs

def is_known_good(name, cmdline, known):
    """判断是否属于已知白名单"""
    if name in SYSTEM_PROCS:
        return True
    if name in known:
        return True
    # 检查命令行中的已知路径
    for known_name in known:
        if known_name in cmdline:
            return True
    # 当前对话相关的进程
    if 'chatbox' in name.lower() or 'claude' in name.lower():
        return True
    # TORK 自己的进程
    if name.startswith('tork_') or name == 'tork' or 'tork' in cmdline:
        return True
    return False

def write_alert(procs):
    """写入警报文件，GUI 会读取"""
    alert = {
        'timestamp': time.time(),
        'procs': [{
            'pid': p['pid'],
            'name': p['name'],
            'cpu': round(p['cpu'], 1),
            'mem_mb': round(p['mem_mb'], 1),
            'cmdline': p['cmdline'][:120],
        } for p in procs],
    }
    try:
        with open(ALERT_FILE, 'w') as f:
            json.dump(alert, f)
    except Exception as e:
        print(f"  WATCHDOGD: write alert failed: {e}", file=sys.stderr)

def clear_alert():
    try:
        if ALERT_FILE.exists():
            ALERT_FILE.unlink()
    except:
        pass

def main():
    print("  WATCHDOGD: 看门狗启动，每 %ds 扫描一次" % POLL_INTERVAL)
    
    # 写 PID
    with open(PID_FILE, 'w') as f:
        f.write(str(os.getpid()))
    
    known = load_known()
    alerted_pids = set()  # 已经弹过窗的 PID，不再重复弹
    
    while True:
        try:
            procs = get_proc_list()
            suspects = []
            
            for p in procs:
                # 跳过自己
                if p['pid'] == os.getpid():
                    continue
                # 跳过已知白名单
                if is_known_good(p['name'], p['cmdline'], known):
                    continue
                # 检查是否超过阈值
                if p['cpu'] > CPU_THRESHOLD or p['mem_mb'] > MEM_THRESHOLD:
                    if p['pid'] not in alerted_pids:
                        suspects.append(p)
            
            if suspects:
                for s in suspects:
                    print(f"  WATCHDOGD: ⚠ 发现高消耗进程: {s['name']}(PID={s['pid']}) CPU={s['cpu']}% MEM={s['mem_mb']}MB")
                    alerted_pids.add(s['pid'])
                write_alert(suspects)
            else:
                clear_alert()
                # 清理已退出的 PID
                current_pids = {p['pid'] for p in procs}
                alerted_pids = {p for p in alerted_pids if p in current_pids}
            
            # 检查 GUI 是否有回复（通过 FLAG_FILE）
            try:
                if FLAG_FILE.exists():
                    with open(FLAG_FILE) as f:
                        cmd = f.read().strip()
                    FLAG_FILE.unlink(missing_ok=True)
                    
                    if cmd.startswith('watchdog_yes:'):
                        pid = int(cmd.split(':')[1])
                        name = cmd.split(':')[2] if len(cmd.split(':')) > 2 else 'unknown'
                        known[name] = {'pid': pid, 'approved_at': time.time()}
                        save_known(known)
                        print(f"  WATCHDOGD: ✓ 已学习: {name}(PID={pid}) 是老板开的")
                        alerted_pids.discard(pid)
                        clear_alert()
                        
                    elif cmd.startswith('watchdog_no:'):
                        pid = int(cmd.split(':')[1])
                        # 干掉它
                        try:
                            os.kill(pid, signal.SIGTERM)
                            print(f"  WATCHDOGD: ✗ 已干掉 PID={pid}")
                            time.sleep(0.5)
                            # 还没死就强制
                            try:
                                os.kill(pid, 0)
                                os.kill(pid, signal.SIGKILL)
                                print(f"  WATCHDOGD: ✗ 已强制杀死 PID={pid}")
                            except:
                                pass
                        except:
                            pass
                        alerted_pids.discard(pid)
                        clear_alert()
            except:
                pass
            
        except Exception as e:
            print(f"  WATCHDOGD: error: {e}", file=sys.stderr)
        
        time.sleep(POLL_INTERVAL)

if __name__ == '__main__':
    # 处理退出信号
    def cleanup(sig, frame):
        clear_alert()
        if PID_FILE.exists():
            PID_FILE.unlink()
        sys.exit(0)
    signal.signal(signal.SIGTERM, cleanup)
    signal.signal(signal.SIGINT, cleanup)
    
    main()
