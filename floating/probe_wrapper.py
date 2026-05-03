"""
probe_wrapper.py — TORK 环境探测 Python 封装

从 C 探测工具读取 JSON 画像, 供仪表盘和云端使用。
用法:
    python3 probe_wrapper.py          # 输出完整 JSON
    python3 probe_wrapper.py --human   # 输出人类可读摘要
"""

import subprocess
import json
import sys
import os

PROBE_BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'probe_env')

def probe():
    """运行 C 探测工具, 返回结构化 dict"""
    if not os.path.exists(PROBE_BIN):
        # 回退: 编译
        src = os.path.join(os.path.dirname(PROBE_BIN), '..', 'install', 'probe_env.c')
        if os.path.exists(src):
            subprocess.run(
                ['gcc', '-O2', '-o', PROBE_BIN, src, '-lrt'],
                capture_output=True
            )
    if os.path.exists(PROBE_BIN):
        result = subprocess.run([PROBE_BIN], capture_output=True, text=True, timeout=5)
        if result.returncode == 0:
            return json.loads(result.stdout)
    # 回退: Python 级基础探测
    return _fallback_probe()

def _fallback_probe():
    """Python 级回退探测(无 C 编译时)"""
    import platform
    import shutil
    
    info = {
        "probe_time": int(__import__('time').time()),
        "cpu": {"brand": "unknown", "physical_cores": os.cpu_count() or 0,
                "logical_threads": os.cpu_count() or 0, "features": {}},
        "os": {"name": platform.platform(), "kernel": platform.release(),
               "arch": platform.machine()},
        "memory": {"total_mb": 0},
        "disk": {"total_mb": 0, "free_mb": 0, "used_pct": 0},
        "toolchain": {
            "gcc": "unknown",
            "python3": sys.version,
            "as": "unknown",
            "make": "unknown"
        }
    }
    # Try toolchain
    for cmd, key in [("gcc --version", "gcc"), ("as --version", "as"),
                     ("make --version", "make")]:
        try:
            r = subprocess.run(cmd.split(), capture_output=True, text=True, timeout=3)
            info["toolchain"][key] = r.stdout.split('\n')[0][:80]
        except: pass
    return info

def human_summary(env: dict) -> str:
    """生成人类可读摘要"""
    lines = []
    cpu = env.get("cpu", {})
    os_ = env.get("os", {})
    mem = env.get("memory", {})
    disk = env.get("disk", {})
    tc = env.get("toolchain", {})
    
    brand = cpu.get("brand", "").strip() or "Unknown CPU"
    lines.append(f"🧠 CPU:  {brand}")
    lines.append(f"        {cpu.get('physical_cores', '?')}核 / {cpu.get('logical_threads', '?')}线程")
    features = cpu.get("features", {})
    feat_str = " ".join([
        k.upper() for k, v in features.items() if v
    ]) or "none"
    lines.append(f"       特色: {feat_str}")
    
    lines.append(f"💻 系统: {os_.get('name', '?')} 内核 {os_.get('kernel', '?')}")
    
    mem_total = mem.get('total_mb', 0)
    lines.append(f"📀 内存: {mem_total}MB")
    
    disk_total = disk.get('total_mb', 0)
    disk_pct = disk.get('used_pct', 0)
    lines.append(f"💾 磁盘: {disk_total}MB ({disk_pct:.1f}% 已用)")
    
    tc_str = " / ".join([v for v in [tc.get("gcc",""), tc.get("python3","")] if v and v != "unknown"])
    lines.append(f"🔧 工具: {tc_str}")
    
    return "\n".join(lines)

if __name__ == "__main__":
    if "--human" in sys.argv:
        print(human_summary(probe()))
    else:
        print(json.dumps(probe(), indent=2))
