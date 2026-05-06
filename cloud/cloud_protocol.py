#!/usr/bin/env python3
"""
TORK 云端协议 v2.2 — 接入 DeepSeek + Dashboard 支持

架构:
  云端大脑 (DeepSeek) ←→ JSON 协议 ←→ TORK 本地代理 ←→ Sandbox/Soul
"""
from __future__ import annotations

import json, sys, os, time, struct, subprocess, threading, queue, shlex, tempfile, logging

BASE: str = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(BASE, 'shared'))
sys.path.insert(0, os.path.join(BASE, 'api'))
from soul_parser import parse_soul_full, SOUL_SIZE


def _find_tork_pid() -> tuple[str | None, int | None]:
    """单次 pgrep 查找 TORK 进程 PID"""
    try:
        r: subprocess.CompletedProcess[str] = subprocess.run(
            ["pgrep", "-x", "tork_core,tork_engine"],
            capture_output=True, text=True)
        if r.returncode != 0:
            return None, None
        first_line: str = r.stdout.strip().split("\n")[0]
        return first_line, int(first_line)
    except (OSError, ValueError):
        return None, None

class TorkCloudAgent:
    """云端代理：接收云端指令，执行本地操作，返回结果"""

    TOOLS: dict[str, dict[str, str]] = {
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

    def __init__(self) -> None:
        self.soul_cache: dict | None = None
        self.msg_id: int = 0
        self.deepseek_api: TorkAPI | None = None
        try:
            from tork_api import TorkAPI
            self.deepseek_api = TorkAPI()
        except Exception:
            pass

    def next_id(self) -> str:
        self.msg_id += 1
        return f"cmd_{self.msg_id}_{int(time.time())}"

    def handle(self, instruction: dict) -> dict:
        cmd_id: str = instruction.get("id", self.next_id())
        tool: str = instruction.get("tool", "")
        args: dict = instruction.get("args", {})
        timeout: int = instruction.get("timeout", 30)

        handlers: dict[str, object] = {
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

        handler: object | None = handlers.get(tool)
        if not handler:
            return {"type": "error", "id": cmd_id, "data": {"msg": f"Unknown tool: {tool}"}}

        try:
            result: dict = handler()
            if isinstance(result, dict) and "id" not in result:
                result["id"] = cmd_id
            return result
        except Exception as e:
            return {"type": "error", "id": cmd_id, "data": {"msg": str(e)}}

    def _run_shell(self, command: str, timeout: int) -> dict:
        sandbox_bin: str = os.path.join(BASE, "build", "tork_sandbox")
        if not os.path.exists(sandbox_bin):
            return {"type": "error", "data": {"msg": "Sandbox not available, command refused"}}
        try:
            r: subprocess.CompletedProcess[str] = subprocess.run([sandbox_bin, command, str(timeout)],
                             capture_output=True, timeout=timeout+5)
            out: dict = json.loads(r.stdout) if r.stdout else {"stdout": "", "stderr": r.stderr.decode()}
            out["type"] = "result"
            return out
        except subprocess.TimeoutExpired:
            return {"type": "result", "data": {
                "exit_code": -1, "stdout": "", "stderr": "TIMEOUT", "timed_out": True
            }}
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}

    def _read_soul_raw(self) -> tuple[str | None, int | None]:
        """读取 Soul 原始字节 hex"""
        try:
            pid_str, pid = _find_tork_pid()
            if pid_str is None:
                return None, None
            with open(f"/proc/{pid}/mem", "rb") as f:
                f.seek(0x200000)
                data: bytes = f.read(SOUL_SIZE)
            return data.hex(), pid
        except (OSError, ValueError):
            return None, None

    def _read_soul(self) -> dict:
        try:
            raw_hex: str | None
            pid: int | None
            raw_hex, pid = self._read_soul_raw()
            if raw_hex is None:
                return {"type": "error", "data": {"msg": "TORK 未运行"}}
            raw: bytes = bytes.fromhex(raw_hex)
            soul: dict = parse_soul_full(raw)
            soul["pid"] = pid
            self.soul_cache = soul
            return {"type": "result", "data": soul}
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}

    def _write_soul(self, offset: int | None, value: int | bytes | None) -> dict:
        if not isinstance(offset, int) or offset < 0 or offset >= SOUL_SIZE:
            return {"type": "error", "data": {"msg": f"Invalid offset: {offset}"}}
        try:
            pid_str, pid = _find_tork_pid()
            if pid_str is None:
                return {"type": "error", "data": {"msg": "TORK 未运行"}}
            import ctypes
            libc: ctypes.CDLL = ctypes.CDLL("libc.so.6")
            libc.ptrace(16, int(pid), None, None)
            try:
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
            finally:
                libc.ptrace(17, int(pid), None, None)
            return {"type": "result", "data": {"status": "ok", "offset": offset}}
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}

    def _validate_path(self, path: str) -> str | None:
        full: str = os.path.normpath(os.path.abspath(path))
        base: str = os.path.normpath(BASE)
        if full == base or full.startswith(base + os.sep):
            return full
        return None

    def _read_file(self, path: str | None) -> dict:
        if path is None:
            return {"type": "error", "data": {"msg": "Path is required"}}
        safe_path: str | None = self._validate_path(path)
        if safe_path is None:
            return {"type": "error", "data": {"msg": "Path outside project directory"}}
        try:
            with open(safe_path, "r") as f:
                content: str = f.read()
            return {"type": "result", "data": {"path": safe_path, "content": content, "size": len(content)}}
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}

    def _write_file(self, path: str | None, content: str | None) -> dict:
        if path is None or content is None:
            return {"type": "error", "data": {"msg": "Path and content are required"}}
        safe_path: str | None = self._validate_path(path)
        if safe_path is None:
            return {"type": "error", "data": {"msg": "Path outside project directory"}}
        try:
            os.makedirs(os.path.dirname(safe_path), exist_ok=True)
            with open(safe_path, "w") as f:
                f.write(content)
            return {"type": "result", "data": {"path": safe_path, "size": len(content)}}
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}

    def _mutate(self, filepath: str | None, diff: str | None) -> dict:
        if filepath is None or diff is None:
            return {"type": "error", "data": {"msg": "File and diff are required"}}
        safe_path: str | None = self._validate_path(filepath)
        if safe_path is None:
            return {"type": "error", "data": {"msg": "Path outside project directory"}}
        try:
            fd: int
            diff_path: str
            fd, diff_path = tempfile.mkstemp(prefix="tork_mutate_")
            try:
                with os.fdopen(fd, "w") as f:
                    f.write(diff)
                r: subprocess.CompletedProcess[str] = subprocess.run(["patch", safe_path, diff_path], capture_output=True, text=True)
            finally:
                os.unlink(diff_path)
            if r.returncode == 0:
                return {"type": "result", "data": {"file": safe_path, "status": "patched"}}
            return {"type": "error", "data": {"msg": r.stderr}}
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}

    def _inbox(self, message: str | None) -> dict:
        if message is None:
            return {"type": "error", "data": {"msg": "Message is required"}}
        try:
            inbox_path: str = os.path.join(BASE, "inbox.md")
            with open(inbox_path, "a") as f:
                f.write(f"\n## ☁️ 云端消息 @ {time.ctime()}\n\n{message}\n")
            return {"type": "result", "data": {"status": "delivered"}}
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}

    def _compile(self) -> dict:
        try:
            r: subprocess.CompletedProcess[str] = subprocess.run(["make", "-C", BASE, "all"],
                             capture_output=True, text=True, timeout=60)
            return {"type": "result", "data": {
                "exit_code": r.returncode,
                "stdout": r.stdout[-500:],
                "stderr": r.stderr[-500:]
            }}
        except subprocess.TimeoutExpired:
            return {"type": "error", "data": {"msg": "compile timeout"}}

    def _ask_deepseek(self, prompt: str | None, temperature: float = 0.5) -> dict:
        if not self.deepseek_api:
            return {"type": "error", "data": {"msg": "DeepSeek API 未配置"}}
        try:
            reply: str = self.deepseek_api.ask_simple(prompt or "", temperature=temperature)
            return {"type": "result", "data": {"reply": reply, "model": self.deepseek_api.model}}
        except Exception as e:
            return {"type": "error", "data": {"msg": str(e)}}

    def _status(self) -> dict:
        info: dict = {"project": "TORK", "base": BASE}
        try:
            r: subprocess.CompletedProcess[str] = subprocess.run(
                ["pgrep", "-x", "tork_core,tork_engine"],
                capture_output=True, text=True)
            if r.returncode == 0:
                pids: list[str] = r.stdout.strip().split("\n")
                info["tork_core_running"] = any("tork_core" in p for p in pids) or True
                info["tork_engine_running"] = True
                info["tork_core_pid"] = pids[0] if pids else ""
                info["tork_engine_pid"] = pids[-1] if len(pids) > 1 else pids[0] if pids else ""
            else:
                info["tork_core_running"] = False
                info["tork_engine_running"] = False
        except Exception:
            info["tork_core_running"] = False
            info["tork_engine_running"] = False
        r = subprocess.run(["git", "-C", BASE, "log", "--oneline", "-1"], capture_output=True, text=True)
        info["last_commit"] = r.stdout.strip() if r.returncode == 0 else "unknown"
        info["agreement"] = os.path.exists("/etc/tork/.agreed")
        info["api_configured"] = self.deepseek_api is not None and bool(self.deepseek_api.api_key)
        info["api_model"] = self.deepseek_api.model if self.deepseek_api else "none"
        soul_result: dict = self._read_soul()
        if soul_result["type"] == "result":
            info["soul"] = soul_result["data"]
        return {"type": "result", "data": info}

    def _dashboard_status(self) -> dict:
        """仪表盘专用：一次性拉取所有状态"""
        info: dict = {}

        # 引擎状态 (单次 pgrep)
        try:
            r: subprocess.CompletedProcess[str] = subprocess.run(
                ["pgrep", "-x", "tork_core,tork_engine"],
                capture_output=True, text=True)
            info["tork_core_running"] = r.returncode == 0
            info["tork_engine_running"] = r.returncode == 0
            info["tork_core_pid"] = r.stdout.strip().split("\n")[0] if r.returncode == 0 else 0
            info["tork_engine_pid"] = r.stdout.strip().split("\n")[-1] if r.returncode == 0 else 0
        except Exception:
            info["tork_core_running"] = False
            info["tork_engine_running"] = False

        # Soul 原始 hex
        raw_hex: str | None
        pid: int | None
        raw_hex, pid = self._read_soul_raw()
        info["soul_hex"] = raw_hex or ""
        info["engine_pid"] = pid or 0

        # 解析 Soul (通过共享 soul_parser)
        if raw_hex:
            try:
                raw: bytes = bytes.fromhex(raw_hex)
                info["soul"] = parse_soul_full(raw)
            except Exception as e:
                info["soul_parse_error"] = str(e)

        # 进化日志
        evo_path: str = os.path.join(BASE, "persist", "evolution.json")
        if os.path.exists(evo_path):
            try:
                with open(evo_path) as f:
                    evo_data: list | dict = json.load(f)
                    info["evolution_log"] = evo_data[-50:] if isinstance(evo_data, list) else []
            except (json.JSONDecodeError, OSError): pass
        else:
            info["evolution_log"] = []

        # 身份信息
        identity_path: str = os.path.join(BASE, "persist", "identity.json")
        if os.path.exists(identity_path):
            try:
                with open(identity_path) as f:
                    info["identity"] = json.load(f)
            except (json.JSONDecodeError, OSError): pass

        # 协议状态
        info["agreement_file"] = os.path.exists("/etc/tork/.agreed")

        # API 状态
        info["api_configured"] = self.deepseek_api is not None and bool(self.deepseek_api.api_key)
        info["api_model"] = self.deepseek_api.model if self.deepseek_api else "none"

        # Git 信息
        r: subprocess.CompletedProcess[str] = subprocess.run(["git", "-C", BASE, "log", "--oneline", "-5"], capture_output=True, text=True)
        info["git_log"] = r.stdout.strip() if r.returncode == 0 else ""
        r = subprocess.run(["git", "-C", BASE, "rev-parse", "--short", "HEAD"], capture_output=True, text=True)
        info["git_hash"] = r.stdout.strip() if r.returncode == 0 else ""

        return {"type": "result", "data": info}


def main() -> None:
    agent: TorkCloudAgent = TorkCloudAgent()
    
    banner: str = json.dumps({
        "type": "ready",
        "version": "2.2",
        "tools": list(TorkCloudAgent.TOOLS.keys()),
        "api_configured": agent.deepseek_api is not None and bool(agent.deepseek_api.api_key),
        "api_model": agent.deepseek_api.model if agent.deepseek_api else "none"
    })
    print(banner, flush=True)

    line: str
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            instruction: dict = json.loads(line)
            result: dict = agent.handle(instruction)
            print(json.dumps(result), flush=True)
        except json.JSONDecodeError as e:
            error: dict = {"type": "error", "id": "parse", "data": {"msg": f"JSON parse error: {e}"}}
            print(json.dumps(error), flush=True)
        except Exception as e:
            error = {"type": "error", "id": "fatal", "data": {"msg": str(e)}}
            print(json.dumps(error), flush=True)


if __name__ == "__main__":
    main()
