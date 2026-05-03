#!/usr/bin/env python3
"""
TORK 云端协议 — TORK 与云端大脑的通信层

架构:
  用户/LLM 云端大脑 (DeepSeek/Claude/GPT)
       ↕ JSON 协议 (stdin/stdout)
   TORK Cloud Agent (本模块)
       ↕ Sandbox API  +  Soul Access
   TORK Core + Engine (本地进程)

协议格式 (TORK → 云端):
  { "type": "observe", "data": { "tick": N, "drive": N, "hw_stress": N, ... } }
  { "type": "result", "id": "cmd_xxx", "data": { "exit_code": 0, "stdout": "...", ... } }

协议格式 (云端 → TORK):
  { "type": "instruct", "id": "cmd_xxx", "tool": "run_shell", "args": {"command": "ls -la"}, "timeout": 30 }
  { "type": "instruct", "id": "cmd_xxx", "tool": "read_soul", "args": {} }
  { "type": "instruct", "id": "cmd_xxx", "tool": "write_soul", "args": {"offset": 0x30, "value": 42} }
  { "type": "instruct", "id": "cmd_xxx", "tool": "read_file", "args": {"path": "..."} }
  { "type": "instruct", "id": "cmd_xxx", "tool": "mutate", "args": {"file": "...", "diff": "..."} }
"""

import json, sys, os, time, struct, subprocess, threading, queue

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

class TorkCloudAgent:
    """云端代理：接收云端指令，执行本地操作，返回结果"""

    TOOLS = {
        "run_shell":    {"desc": "Execute shell command via sandbox"},
        "read_soul":    {"desc": "Read TORK Soul (96 bytes at 0x200000)"},
        "write_soul":   {"desc": "Write to TORK Soul field"},
        "read_file":    {"desc": "Read file content"},
        "write_file":   {"desc": "Write file content"},
        "mutate":       {"desc": "Apply code mutation to TORK source"},
        "inbox":        {"desc": "Write to inbox.md for TORK to read"},
        "compile":      {"desc": "Compile TORK engine/core"},
        "status":       {"desc": "Get TORK system status"},
        "think":        {"desc": "Cloud brain can store reasoning notes"},
    }

    def __init__(self):
        self.soul_cache = None
        self.msg_id = 0

    def next_id(self):
        self.msg_id += 1
        return f"cmd_{self.msg_id}_{int(time.time())}"

    def handle(self, instruction):
        """Handle one instruction from cloud brain"""
        cmd_id = instruction.get("id", self.next_id())
        tool = instruction.get("tool", "")
        args = instruction.get("args", {})
        timeout = instruction.get("timeout", 30)

        if tool == "run_shell":
            return self._run_shell(args.get("command", ""), timeout)
        elif tool == "read_soul":
            return self._read_soul()
        elif tool == "write_soul":
            return self._write_soul(args.get("offset"), args.get("value"))
        elif tool == "read_file":
            return self._read_file(args.get("path"))
        elif tool == "write_file":
            return self._write_file(args.get("path"), args.get("content"))
        elif tool == "mutate":
            return self._mutate(args.get("file"), args.get("diff"))
        elif tool == "inbox":
            return self._inbox(args.get("message"))
        elif tool == "compile":
            return self._compile()
        elif tool == "status":
            return self._status()
        elif tool == "think":
            return {"type": "ack", "id": cmd_id, "data": {"status": "noted"}}
        else:
            return {"type": "error", "id": cmd_id, "data": {"msg": f"Unknown tool: {tool}"}}

    def _run_shell(self, command, timeout):
        """Execute via sandbox (the C sandbox binary or fallback to subprocess)"""
        cmd_id = self.next_id()
        
        # Try to use the C sandbox binary if available
        sandbox_bin = os.path.join(BASE, "build", "tork_sandbox")
        if os.path.exists(sandbox_bin):
            import subprocess as sp
            try:
                r = sp.run([sandbox_bin, command, str(timeout)],
                          capture_output=True, timeout=timeout+5)
                out = json.loads(r.stdout) if r.stdout else {"stdout": "", "stderr": r.stderr.decode()}
                out["type"] = "result"
                out["id"] = cmd_id
                return out
            except:
                pass
        
        # Fallback: Python subprocess with timeout
        try:
            r = subprocess.run(command, shell=True, capture_output=True,
                              timeout=timeout, text=True)
            return {
                "type": "result",
                "id": cmd_id,
                "data": {
                    "exit_code": r.returncode,
                    "stdout": r.stdout,
                    "stderr": r.stderr,
                    "timed_out": False
                }
            }
        except subprocess.TimeoutExpired:
            return {
                "type": "result",
                "id": cmd_id,
                "data": {
                    "exit_code": -1,
                    "stdout": "",
                    "stderr": "TIMEOUT",
                    "timed_out": True
                }
            }
        except Exception as e:
            return {"type": "error", "id": cmd_id, "data": {"msg": str(e)}}

    def _read_soul(self):
        """Read the 96-byte Soul from the TORK core process"""
        try:
            # Find tork_core PID
            r = subprocess.run(["pgrep", "-x", "tork_core"], capture_output=True, text=True)
            if r.returncode != 0:
                return {"type": "error", "id": "soul", "data": {"msg": "TORK core not running"}}
            pid = r.stdout.strip().split("\n")[0]
            
            # Read /proc/PID/mem at 0x200000
            with open(f"/proc/{pid}/mem", "rb") as f:
                f.seek(0x200000)
                data = f.read(96)
            
            # Parse into fields
            soul = {
                "tick": struct.unpack_from("<I", data, 0x00)[0],
                "last_tsc": struct.unpack_from("<Q", data, 0x04)[0],
                "cur_tsc": struct.unpack_from("<Q", data, 0x0C)[0],
                "elapsed": struct.unpack_from("<Q", data, 0x14)[0],
                "expected": struct.unpack_from("<Q", data, 0x1C)[0],
                "hw_stress": data[0x24],
                "mode": data[0x25],
                "crc": struct.unpack_from("<I", data, 0x28)[0],
                "self_pid": struct.unpack_from("<I", data, 0x2C)[0],
                "drive": struct.unpack_from("<b", data, 0x30)[0],
                "ppid": struct.unpack_from("<H", data, 0x32)[0],
                "code_insns": struct.unpack_from("<H", data, 0x34)[0],
                "code_ctrl": struct.unpack_from("<H", data, 0x3A)[0],
                "code_mod_success": data[0x3E],
                "code_opt_saved": data[0x3F],
                "fission_count": data[0x41],
                "wins": struct.unpack_from("<H", data, 0x46)[0],
            }
            
            # New fields
            if len(data) >= 0x50:
                soul["agreed"] = data[0x48] if 0x48 < len(data) else 0
                soul["sandbox_level"] = data[0x49] if 0x49 < len(data) else 0
                soul["cloud_connected"] = data[0x4A] if 0x4A < len(data) else 0
                soul["learn_count"] = struct.unpack_from("<H", data, 0x4C)[0] if 0x4C+2 <= len(data) else 0
                soul["mutation_count"] = struct.unpack_from("<H", data, 0x4E)[0] if 0x4E+2 <= len(data) else 0
                soul["gen_count"] = struct.unpack_from("<I", data, 0x54)[0] if 0x54+4 <= len(data) else 0
            
            self.soul_cache = soul
            return {"type": "result", "id": "soul", "data": soul}
        except Exception as e:
            return {"type": "error", "id": "soul", "data": {"msg": str(e)}}

    def _write_soul(self, offset, value):
        """Write a value to the TORK Soul"""
        try:
            r = subprocess.run(["pgrep", "-x", "tork_core"], capture_output=True, text=True)
            if r.returncode != 0:
                return {"type": "error", "id": "soul_write", "data": {"msg": "TORK core not running"}}
            pid = r.stdout.strip()
            
            import ctypes
            # Use ptrace to attach and write
            libc = ctypes.CDLL("libc.so.6")
            
            # ptrace attach
            PT_ATTACH = 16
            PT_DETACH = 17
            ret = libc.ptrace(PT_ATTACH, int(pid), None, None)
            if ret != 0:
                return {"type": "error", "id": "soul_write", "data": {"msg": "ptrace attach failed"}}
            
            import time
            time.sleep(0.05)  # wait for stop
            
            # Open /proc/PID/mem for writing
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
            
            libc.ptrace(PT_DETACH, int(pid), None, None)
            return {"type": "result", "id": "soul_write", "data": {"status": "ok", "offset": offset, "value": value}}
        except Exception as e:
            return {"type": "error", "id": "soul_write", "data": {"msg": str(e)}}

    def _read_file(self, path):
        try:
            with open(path, "r") as f:
                content = f.read()
            return {"type": "result", "id": "read", "data": {"path": path, "content": content, "size": len(content)}}
        except Exception as e:
            return {"type": "error", "id": "read", "data": {"msg": str(e)}}

    def _write_file(self, path, content):
        try:
            os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
            with open(path, "w") as f:
                f.write(content)
            return {"type": "result", "id": "write", "data": {"path": path, "size": len(content)}}
        except Exception as e:
            return {"type": "error", "id": "write", "data": {"msg": str(e)}}

    def _mutate(self, filepath, diff):
        """Apply a diff/patch to a TORK source file"""
        try:
            # Write diff to temp file
            diff_path = f"/tmp/tork_mutate_{int(time.time())}.diff"
            with open(diff_path, "w") as f:
                f.write(diff)
            
            r = subprocess.run(["patch", filepath, diff_path], capture_output=True, text=True)
            os.unlink(diff_path)
            
            if r.returncode == 0:
                return {"type": "result", "id": "mutate", "data": {"file": filepath, "status": "patched"}}
            else:
                return {"type": "error", "id": "mutate", "data": {"msg": r.stderr}}
        except Exception as e:
            return {"type": "error", "id": "mutate", "data": {"msg": str(e)}}

    def _inbox(self, message):
        try:
            inbox_path = os.path.join(BASE, "inbox.md")
            with open(inbox_path, "a") as f:
                f.write(f"\n## Cloud message @ {time.ctime()}\n\n{message}\n")
            return {"type": "result", "id": "inbox", "data": {"status": "delivered"}}
        except Exception as e:
            return {"type": "error", "id": "inbox", "data": {"msg": str(e)}}

    def _compile(self):
        try:
            r = subprocess.run(["make", "-C", BASE, "clean", "all"],
                             capture_output=True, text=True, timeout=60)
            return {
                "type": "result",
                "id": "compile",
                "data": {
                    "exit_code": r.returncode,
                    "stdout": r.stdout,
                    "stderr": r.stderr
                }
            }
        except subprocess.TimeoutExpired:
            return {"type": "error", "id": "compile", "data": {"msg": "compile timeout"}}
        except Exception as e:
            return {"type": "error", "id": "compile", "data": {"msg": str(e)}}

    def _status(self):
        info = {"project": "TORK", "base": BASE}
        
        # Check if core running
        r = subprocess.run(["pgrep", "-x", "tork_core"], capture_output=True, text=True)
        info["core_running"] = r.returncode == 0
        info["core_pid"] = r.stdout.strip() if r.returncode == 0 else None
        
        # Check if engine running
        r = subprocess.run(["pgrep", "-x", "tork_engine"], capture_output=True, text=True)
        info["engine_running"] = r.returncode == 0
        
        # Last commit
        r = subprocess.run(["git", "-C", BASE, "log", "--oneline", "-1"], capture_output=True, text=True)
        info["last_commit"] = r.stdout.strip() if r.returncode == 0 else "unknown"
        
        # Soul
        soul_result = self._read_soul()
        if soul_result["type"] == "result":
            info["soul"] = soul_result["data"]
        
        # Agreement
        info["agreement"] = os.path.exists("/etc/tork/.agreed")
        
        return {"type": "result", "id": "status", "data": info}


def main():
    """Main entry point: read JSON instructions from stdin, write results to stdout"""
    agent = TorkCloudAgent()
    
    # Print banner
    banner = json.dumps({
        "type": "ready",
        "version": "2.0",
        "tools": list(TorkCloudAgent.TOOLS.keys()),
        "soul_size": 96,
        "soul_base": "0x200000"
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
