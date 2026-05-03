#!/usr/bin/env python3
"""
TORK 云端协议 v2.1 — 接入 DeepSeek

架构:
  云端大脑 (DeepSeek) ←→ JSON 协议 ←→ TORK 本地代理 ←→ Sandbox/Soul
"""

import json, sys, os, time, struct, subprocess, threading, queue

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(BASE, 'api'))

class TorkCloudAgent:
    """云端代理：接收云端指令，执行本地操作，返回结果"""

    TOOLS = {
        "run_shell":    {"desc": "通过沙箱执行 shell 命令"},
        "read_soul":    {"desc": "读取 TORK Soul (96 bytes @ 0x200000)"},
        "write_soul":   {"desc": "写入 TORK Soul 字段"},
        "read_file":    {"desc": "读取文件"},
        "write_file":   {"desc": "写入文件"},
        "mutate":       {"desc": "应用代码变异"},
        "inbox":        {"desc": "写入 inbox.md"},
        "compile":      {"desc": "编译 TORK"},
        "status":       {"desc": "系统状态"},
        "think":        {"desc": "云端思考记录"},
        "ask_deepseek": {"desc": "向 DeepSeek 提问"},
    }

    def __init__(self):
        self.soul_cache = None
        self.msg_id = 0
        self.deepseek_api = None
        try:
            from tork_api import TorkAPI
            self.deepseek_api = TorkAPI()
        except:
            pass

    def next_id(self):
        self.msg_id += 1
        return f"cmd_{self.msg_id}_{int(time.time())}"

    def handle(self, instruction):
        cmd_id = instruction.get("id", self.next_id())
        tool = instruction.get("tool", "")
        args = instruction.get("args", {})
        timeout = instruction.get("timeout", 30)

        handlers = {
            "run_shell":    lambda: self._run_shell(args.get("command", ""), timeout),
            "read_soul":    lambda: self._read_soul(),
            "write_soul":   lambda: self._write_soul(args.get("offset"), args.get("value")),
            "read_file":    lambda: self._read_file(args.get("path")),
            "write_file":   lambda: self._write_file(args.get("path"), args.get("content")),
            "mutate":       lambda: self._mutate(args.get("file"), args.get("diff")),
            "inbox":        lambda: self._inbox(args.get("message")),
            "compile":      lambda: self._compile(),
            "status":       lambda: self._status(),
            "think":        lambda: {"type": "ack", "id": cmd_id, "data": {"status": "noted"}},
            "ask_deepseek": lambda: self._ask_deepseek(args.get("prompt"), args.get("temperature", 0.5)),
        }

        handler = handlers.get(tool)
        if not handler:
            return {"type": "error", "id": cmd_id, "data": {"msg": f"Unknown tool: {tool}"}}

        try:
            result = handler()
            if isinstance(result, dict) and "id" not in result:
                result["id"] = cmd_id
            return result
        except Exception as e:
            return {"type": "error", "id": cmd_id, "data": {"msg": str(e)}}

    def _run_shell(self, command, timeout):
        sandbox_bin = os.path.join(BASE, "build", "tork_sandbox")
        if os.path.exists(sandbox_bin):
            try:
                r = subprocess.run([sandbox_bin, command, str(timeout)],
                                 capture_output=True, timeout=timeout+5)
                out = json.loads(r.stdout) if r.stdout else {"stdout": "", "stderr": r.stderr.decode()}
                out["type"] = "result"
                return out
            except:
                pass
        try:
            r = subprocess.run(command, shell=True, capture_output=True,
                             timeout=timeout, text=True)
            return {"type": "result", "data": {
                "exit_code": r.returncode, "stdout": r.stdout,
                "stderr": r.stderr, "timed_out": False
            }}
        except subprocess.TimeoutExpired:
            return {"type": "result", "data": {
                "exit_code": -1, "stdout": "", "stderr": "TIMEOUT", "timed_out": True
            }}
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}

    def _read_soul(self):
        try:
            r = subprocess.run(["pgrep", "-x", "tork_core"], capture_output=True, text=True)
            if r.returncode != 0:
                # 尝试 tork_engine
                r = subprocess.run(["pgrep", "-x", "tork_engine"], capture_output=True, text=True)
                if r.returncode != 0:
                    return {"type": "error", "data": {"msg": "TORK 未运行"}}
            pid = r.stdout.strip().split("\n")[0]
            with open(f"/proc/{pid}/mem", "rb") as f:
                f.seek(0x200000)
                data = f.read(96)
            soul = {
                "tick": struct.unpack_from("<I", data, 0x00)[0],
                "hw_stress": data[0x24],
                "drive": struct.unpack_from("<b", data, 0x30)[0],
                "agreed": data[0x48] if len(data) > 0x48 else 0,
                "sandbox_level": data[0x49] if len(data) > 0x49 else 0,
                "cloud_connected": data[0x4A] if len(data) > 0x4A else 0,
                "learn_count": struct.unpack_from("<H", data, 0x4C)[0] if len(data) > 0x4D else 0,
                "mutation_count": struct.unpack_from("<H", data, 0x4E)[0] if len(data) > 0x4F else 0,
                "gen_count": struct.unpack_from("<I", data, 0x54)[0] if len(data) > 0x57 else 0,
            }
            self.soul_cache = soul
            return {"type": "result", "data": soul}
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}

    def _write_soul(self, offset, value):
        try:
            r = subprocess.run(["pgrep", "-x", "tork_core"], capture_output=True, text=True)
            if r.returncode != 0:
                r = subprocess.run(["pgrep", "-x", "tork_engine"], capture_output=True, text=True)
            if r.returncode != 0:
                return {"type": "error", "data": {"msg": "TORK 未运行"}}
            pid = r.stdout.strip().split("\n")[0]
            import ctypes
            libc = ctypes.CDLL("libc.so.6")
            libc.ptrace(16, int(pid), None, None)  # PTRACE_ATTACH
            time.sleep(0.05)
            with open(f"/proc/{pid}/mem", "rb+") as f:
                f.seek(0x200000 + offset)
                if isinstance(value, int):
                    if value < 256:
                        f.write(bytes([value]))
                    elif value < 65536:
                        f.write(struct.pack("<H", value))
                    else:
                        f.write(struct.pack("<I", value))
                elif isinstance(value, bytes):
                    f.write(value)
            libc.ptrace(17, int(pid), None, None)  # PTRACE_DETACH
            return {"type": "result", "data": {"status": "ok", "offset": offset}}
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}

    def _read_file(self, path):
        try:
            with open(path, "r") as f:
                content = f.read()
            return {"type": "result", "data": {"path": path, "content": content, "size": len(content)}}
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}

    def _write_file(self, path, content):
        try:
            os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
            with open(path, "w") as f:
                f.write(content)
            return {"type": "result", "data": {"path": path, "size": len(content)}}
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}

    def _mutate(self, filepath, diff):
        try:
            diff_path = f"/tmp/tork_mutate_{int(time.time())}.diff"
            with open(diff_path, "w") as f:
                f.write(diff)
            r = subprocess.run(["patch", filepath, diff_path], capture_output=True, text=True)
            os.unlink(diff_path)
            if r.returncode == 0:
                return {"type": "result", "data": {"file": filepath, "status": "patched"}}
            return {"type": "error", "data": {"msg": r.stderr}}
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}

    def _inbox(self, message):
        try:
            inbox_path = os.path.join(BASE, "inbox.md")
            with open(inbox_path, "a") as f:
                f.write(f"\n## ☁️ 云端消息 @ {time.ctime()}\n\n{message}\n")
            return {"type": "result", "data": {"status": "delivered"}}
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}

    def _compile(self):
        try:
            r = subprocess.run(["make", "-C", BASE, "all"],
                             capture_output=True, text=True, timeout=60)
            return {"type": "result", "data": {
                "exit_code": r.returncode,
                "stdout": r.stdout[-500:],
                "stderr": r.stderr[-500:]
            }}
        except subprocess.TimeoutExpired:
            return {"type": "error", "data": {"msg": "compile timeout"}}

    def _ask_deepseek(self, prompt, temperature=0.5):
        if not self.deepseek_api:
            return {"type": "error", "data": {"msg": "DeepSeek API 未配置"}}
        try:
            reply = self.deepseek_api.ask_simple(prompt, temperature=temperature)
            return {"type": "result", "data": {"reply": reply, "model": self.deepseek_api.model}}
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}

    def _status(self):
        info = {"project": "TORK", "base": BASE}
        for proc_name in ["tork_core", "tork_engine"]:
            r = subprocess.run(["pgrep", "-x", proc_name], capture_output=True, text=True)
            info[f"{proc_name}_running"] = r.returncode == 0
            if r.returncode == 0:
                info[f"{proc_name}_pid"] = r.stdout.strip()
        r = subprocess.run(["git", "-C", BASE, "log", "--oneline", "-1"], capture_output=True, text=True)
        info["last_commit"] = r.stdout.strip() if r.returncode == 0 else "unknown"
        info["agreement"] = os.path.exists("/etc/tork/.agreed")
        # API 配置状态
        info["api_configured"] = self.deepseek_api is not None and bool(self.deepseek_api.api_key)
        info["api_model"] = self.deepseek_api.model if self.deepseek_api else "none"
        soul_result = self._read_soul()
        if soul_result["type"] == "result":
            info["soul"] = soul_result["data"]
        return {"type": "result", "data": info}


def main():
    agent = TorkCloudAgent()
    
    banner = json.dumps({
        "type": "ready",
        "version": "2.1",
        "tools": list(TorkCloudAgent.TOOLS.keys()),
        "api_configured": agent.deepseek_api is not None and bool(agent.deepseek_api.api_key),
        "api_model": agent.deepseek_api.model if agent.deepseek_api else "none"
    })
    print(banner, flush=True)

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            instruction = json.loads(line)
            result = agent.handle(instruction)
            print(json.dumps(result), flush=True)
        except json.JSONDecodeError as e:
            error = {"type": "error", "id": "parse", "data": {"msg": f"JSON parse error: {e}"}}
            print(json.dumps(error), flush=True)
        except Exception as e:
            error = {"type": "error", "id": "fatal", "data": {"msg": str(e)}}
            print(json.dumps(error), flush=True)


if __name__ == "__main__":
    main()
