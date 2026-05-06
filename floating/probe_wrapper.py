from __future__ import annotations

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

PROBE_BIN: str = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'probe_env')

def probe() -> dict[str, object]:
    """运行 C 探测工具, 返回结构化 dict"""
    if not os.path.exists(PROBE_BIN):
        # 回退: 编译
        src: str = os.path.join(os.path.dirname(PROBE_BIN), '..', 'install', 'probe_env.c')
        if os.path.exists(src):
            subprocess.run(
                ['gcc', '-O2', '-o', PROBE_BIN, src, '-lrt'],
                capture_output=True
            )
    if os.path.exists(PROBE_BIN):
        result: subprocess.CompletedProcess[str] = subprocess.run([PROBE_BIN], capture_output=True, text=True, timeout=5)
        if result.returncode == 0:
            return json.loads(result.stdout)
    # 回退: Python 级基础探测
    return _fallback_probe()

def _fallback_probe() -> dict[str, object]:
    """Python 级回退探测(无 C 编译时)"""
    import platform
    import shutil
    
    info: dict[str, object] = {
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
            r: subprocess.CompletedProcess[str] = subprocess.run(cmd.split(), capture_output=True, text=True, timeout=3)
            info["toolchain"][key] = r.stdout.split('\n')[0][:80]
        except Exception: pass
    return info

def human_summary(env: dict[str, object]) -> str:
    """生成人类可读摘要"""
    lines: list[str] = []
    cpu: dict[str, object] = env.get("cpu", {})
    os_: dict[str, object] = env.get("os", {})
    mem: dict[str, object] = env.get("memory", {})
    disk: dict[str, object] = env.get("disk", {})
    tc: dict[str, object] = env.get("toolchain", {})
    
    brand: str = cpu.get("brand", "").strip() or "Unknown CPU"
    lines.append(f"🧠 CPU:  {brand}")
    lines.append(f"        {cpu.get('physical_cores', '?')}核 / {cpu.get('logical_threads', '?')}线程")
    features: dict[str, object] = cpu.get("features", {})
    feat_str: str = " ".join([
        k.upper() for k, v in features.items() if v
    ]) or "none"
    lines.append(f"       特色: {feat_str}")
    
    lines.append(f"💻 系统: {os_.get('name', '?')} 内核 {os_.get('kernel', '?')}")
    
    mem_total: int = mem.get('total_mb', 0)
    lines.append(f"📀 内存: {mem_total}MB")
    
    disk_total: int = disk.get('total_mb', 0)
    disk_pct: int = disk.get('used_pct', 0)
    lines.append(f"💾 磁盘: {disk_total}MB ({disk_pct:.1f}% 已用)")
    
    tc_str: str = " / ".join([v for v in [tc.get("gcc",""), tc.get("python3","")] if v and v != "unknown"])
    lines.append(f"🔧 工具: {tc_str}")
    
    return "\n".join(lines)

if __name__ == "__main__":
    if "--human" in sys.argv:
        print(human_summary(probe()))
    else:
        print(json.dumps(probe(), indent=2))
