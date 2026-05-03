#!/usr/bin/env python3
"""
TORK 云端协议 v2.2 — 接入 DeepSeek + Dashboard 支持

架构:
  云端大脑 (DeepSeek) ←→ JSON 协议 ←→ TORK 本地代理 ←→ Sandbox/Soul
"""

import json, sys, os, time, struct, subprocess, threading, queue

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(BASE, 'api'))

class TorkCloudAgent:
    """云端代理：接收云端指令，执行本地操作，返回结果"""

    TOOLS = {
        "run_shell":        {"desc": "通过沙箱执行 shell 命令"},
        "read_soul":        {"desc": "读取 TORK Soul (96 bytes @ 0x200000)"},
        "write_soul":       {"desc": "写入 TORK Soul 字段"},
        "read_file":        {"desc": "读取文件"},
        "write_file":       {"desc": "写入文件"},
        "mutate":           {"desc": "应用代码变异"},
        "inbox":            {"desc": "写入 inbox.md"},
        "compile":          {"desc": "编译 TORK"},
        "status":           {"desc": "系统状态"},
        "dashboard_status": {"desc": "仪表盘全量状态（一次性拉取）"},
        "think":            {"desc": "云端思考记录"},
        "ask_deepseek":     {"desc": "向 DeepSeek 提问"},
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
            "run_shell":        lambda: self._run_shell(args.get("command", ""), timeout),
            "read_soul":        lambda: self._read_soul(),
            "write_soul":       lambda: self._write_soul(args.get("offset"), args.get("value")),
            "read_file":        lambda: self._read_file(args.get("path")),
            "write_file":       lambda: self._write_file(args.get("path"), args.get("content")),
            "mutate":           lambda: self._mutate(args.get("file"), args.get("diff")),
            "inbox":            lambda: self._inbox(args.get("message")),
            "compile":          lambda: self._compile(),
            "status":           lambda: self._status(),
            "dashboard_status": lambda: self._dashboard_status(),
            "think":            lambda: {"type": "ack", "id": cmd_id, "data": {"status": "noted"}},
            "ask_deepseek":     lambda: self._ask_deepseek(args.get("prompt"), args.get("temperature", 0.5)),
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

    def _read_soul_raw(self):
        """读取 Soul 原始 96 字节 hex"""
        try:
            r = subprocess.run(["pgrep", "-x", "tork_core"], capture_output=True, text=True)
            if r.returncode != 0:
                r = subprocess.run(["pgrep", "-x", "tork_engine"], capture_output=True, text=True)
                if r.returncode != 0:
                    return None, None
            pid = r.stdout.strip().split("\n")[0]
            with open(f"/proc/{pid}/mem", "rb") as f:
                f.seek(0x200000)
                data = f.read(96)
            return data.hex(), int(pid)
        except:
            return None, None

    def _read_soul(self):
        try:
            raw_hex, pid = self._read_soul_raw()
            if raw_hex is None:
                return {"type": "error", "data": {"msg": "TORK 未运行"}}
            raw = bytes.fromhex(raw_hex)
            soul = {
                "tick": struct.unpack_from("<I", raw, 0x00)[0],
                "hw_stress": raw[0x24],
                "drive": struct.unpack_from("<b", raw, 0x30)[0],
                "agreed": raw[0x48] if len(raw) > 0x48 else 0,
                "sandbox_level": raw[0x49] if len(raw) > 0x49 else 0,
                "cloud_connected": raw[0x4A] if len(raw) > 0x4A else 0,
                "learn_count": struct.unpack_from("<H", raw, 0x4C)[0] if len(raw) > 0x4D else 0,
                "mutation_count": struct.unpack_from("<H", raw, 0x4E)[0] if len(raw) > 0x4F else 0,
                "gen_count": struct.unpack_from("<I", raw, 0x54)[0] if len(raw) > 0x57 else 0,
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
            libc.ptrace(16, int(pid), None, None)
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
            libc.ptrace(17, int(pid), None, None)
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
        info["api_configured"] = self.deepseek_api is not None and bool(self.deepseek_api.api_key)
        info["api_model"] = self.deepseek_api.model if self.deepseek_api else "none"
        soul_result = self._read_soul()
        if soul_result["type"] == "result":
            info["soul"] = soul_result["data"]
        return {"type": "result", "data": info}

    def _dashboard_status(self):
        """仪表盘专用：一次性拉取所有状态"""
        info = {}

        # 引擎状态
        for proc_name in ["tork_core", "tork_engine"]:
            r = subprocess.run(["pgrep", "-x", proc_name], capture_output=True, text=True)
            info[f"{proc_name}_running"] = r.returncode == 0
            info[f"{proc_name}_pid"] = r.stdout.strip() if r.returncode == 0 else 0

        # Soul 原始 hex
        raw_hex, pid = self._read_soul_raw()
        info["soul_hex"] = raw_hex or ""
        info["engine_pid"] = pid or 0

        # 解析 Soul (v2.0 layout — 与 tork_soul.inc 一致)
        if raw_hex:
            try:
                raw = bytes.fromhex(raw_hex)
                soul = {}
                soul["tick"]           = struct.unpack_from("<I", raw, 0x00)[0]
                soul["last_tsc"]       = struct.unpack_from("<Q", raw, 0x04)[0]
                soul["cur_tsc"]        = struct.unpack_from("<Q", raw, 0x0C)[0]
                soul["elapsed"]        = struct.unpack_from("<Q", raw, 0x14)[0]
                soul["expected"]       = struct.unpack_from("<Q", raw, 0x1C)[0]
                soul["hw_stress"]      = raw[0x24]
                soul["mode"]           = raw[0x25]
                soul["crc"]            = struct.unpack_from("<I", raw, 0x28)[0]
                soul["self_pid"]       = struct.unpack_from("<I", raw, 0x2C)[0]
                soul["drive"]          = struct.unpack_from("<b", raw, 0x30)[0]
                soul["ppid"]           = struct.unpack_from("<H", raw, 0x32)[0]
                soul["code_insns"]     = struct.unpack_from("<H", raw, 0x34)[0]
                soul["code_mov"]       = struct.unpack_from("<H", raw, 0x36)[0]
                soul["code_arith"]     = struct.unpack_from("<H", raw, 0x38)[0]
                soul["code_ctrl"]      = struct.unpack_from("<H", raw, 0x3A)[0]
                soul["code_other"]     = struct.unpack_from("<H", raw, 0x3C)[0]
                soul["mod_success"]    = raw[0x3E]
                soul["opt_saved"]      = raw[0x3F]
                soul["nop_count"]      = raw[0x40]
                soul["fission_count"]  = raw[0x41]
                soul["child_pid"]      = struct.unpack_from("<H", raw, 0x42)[0]
                soul["fission_tick"]   = struct.unpack_from("<H", raw, 0x44)[0]
                soul["wins"]           = struct.unpack_from("<H", raw, 0x46)[0]
                soul["agreed"]         = raw[0x48]
                soul["sandbox_level"]  = raw[0x49]
                soul["cloud_connected"]= raw[0x4A]
                soul["cloud_provider"] = raw[0x4B]
                soul["learn_count"]    = struct.unpack_from("<H", raw, 0x4C)[0]
                soul["mutation_count"] = struct.unpack_from("<H", raw, 0x4E)[0]
                soul["best_score"]     = struct.unpack_from("<I", raw, 0x50)[0]
                soul["gen_count"]      = struct.unpack_from("<I", raw, 0x54)[0]
                info["soul"] = soul
            except Exception as e:
                info["soul_parse_error"] = str(e)

        # 进化日志
        evo_path = os.path.join(BASE, "persist", "evolution.json")
        if os.path.exists(evo_path):
            try:
                with open(evo_path) as f:
                    evo_data = json.load(f)
                    info["evolution_log"] = evo_data[-50:] if isinstance(evo_data, list) else []
            except: pass
        else:
            info["evolution_log"] = []

        # 身份信息
        identity_path = os.path.join(BASE, "persist", "identity.json")
        if os.path.exists(identity_path):
            try:
                with open(identity_path) as f:
                    info["identity"] = json.load(f)
            except: pass

        # 协议状态
        info["agreement_file"] = os.path.exists("/etc/tork/.agreed")

        # API 状态
        info["api_configured"] = self.deepseek_api is not None and bool(self.deepseek_api.api_key)
        info["api_model"] = self.deepseek_api.model if self.deepseek_api else "none"

        # Git 信息
        r = subprocess.run(["git", "-C", BASE, "log", "--oneline", "-5"], capture_output=True, text=True)
        info["git_log"] = r.stdout.strip() if r.returncode == 0 else ""
        r = subprocess.run(["git", "-C", BASE, "rev-parse", "--short", "HEAD"], capture_output=True, text=True)
        info["git_hash"] = r.stdout.strip() if r.returncode == 0 else ""

        return {"type": "result", "data": info}


def main():
    agent = TorkCloudAgent()
    
    banner = json.dumps({
        "type": "ready",
        "version": "2.2",
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
