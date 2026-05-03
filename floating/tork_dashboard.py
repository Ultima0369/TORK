#!/usr/bin/env python3
"""
TORK 生命仪表盘 v2.3
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
显示心跳、本能、进化状态、云端连接
通过 Cloud Protocol + TorkAPI 双通道通信
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, simpledialog, messagebox
import subprocess
import json
import os
import time
import threading
import sys
import signal
import struct

# ─── 路径 ────────────────────────────────────────────
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(BASE_DIR, 'api'))

# ─── 配置 ────────────────────────────────────────────
CONFIG = {
    "refresh_ms": 1000,
    "cloud_script": os.path.join(BASE_DIR, "cloud", "cloud_protocol.py"),
    "persist_dir": os.path.join(BASE_DIR, "persist"),
    "api_config_path": os.path.join(BASE_DIR, "api", "api_config.json"),
    "soul_layout": {
        "tick": 0x00, "hw_stress": 0x24, "mode": 0x25,
        "drive": 0x30, "agreed": 0x48, "sandbox_level": 0x49,
        "cloud_connected": 0x4A, "cloud_provider": 0x4B,
        "learn_count": 0x4C, "mutation_count": 0x4E,
        "best_score": 0x50, "gen_count": 0x54,
    },
}

# ─── 颜色主题 ────────────────────────────────────────
THEME = {
    "bg": "#0a0a0f", "fg": "#c0c8d0",
    "accent": "#4ec9b0", "accent2": "#569cd6",
    "warn": "#dcdcaa", "danger": "#f44747",
    "success": "#6a9955", "dim": "#606060",
    "panel_bg": "#13131a", "border": "#1e1e2e",
    "font": ("Consolas", 10), "font_bold": ("Consolas", 10, "bold"),
    "font_small": ("Consolas", 8), "font_large": ("Consolas", 14, "bold"),
}


# ─── API 加载器 ──────────────────────────────────────
def load_api():
    """加载 TorkAPI，返回 (api, error_msg)"""
    try:
        from tork_api import TorkAPI
        api = TorkAPI()
        if not api.api_key:
            return None, "API Key 未配置"
        return api, None
    except Exception as e:
        return None, str(e)


def load_api_config():
    """读取 API 配置文件"""
    cfg_path = CONFIG["api_config_path"]
    if os.path.exists(cfg_path):
        try:
            with open(cfg_path) as f:
                return json.load(f)
        except:
            pass
    return {"base_url": "https://api.deepseek.com", "model": "deepseek-v4-pro", "api_key": ""}


def save_api_config(cfg):
    """保存 API 配置文件"""
    os.makedirs(os.path.dirname(CONFIG["api_config_path"]), exist_ok=True)
    with open(CONFIG["api_config_path"], "w") as f:
        json.dump(cfg, f, indent=2)


# ─── 全局状态 ────────────────────────────────────────
class TORKState:
    def __init__(self):
        self.soul = {}
        self.cloud_status = "disconnected"
        self.evolution_log = []
        self.chat_history = []
        self.engine_running = False
        self.pid = 0
        self.generation = 0
        self.mutation_success = 0
        self.mutation_total = 0
        self.instincts = {"fear": 0.0, "desire": 0.0, "curiosity": 0.0}
        self.last_error = ""
        self.agreement_status = "unknown"
        self.sandbox_level_name = "none"
        self.api = None
        self.api_error = ""

    def load_identity(self):
        id_file = os.path.join(CONFIG["persist_dir"], "identity.json")
        if os.path.exists(id_file):
            try:
                with open(id_file) as f:
                    data = json.load(f)
                    self.generation = data.get("generation", 0)
                    self.mutation_success = data.get("mutation_success", 0)
                    self.mutation_total = data.get("mutation_total", 0)
            except: pass

    def save_identity(self):
        os.makedirs(CONFIG["persist_dir"], exist_ok=True)
        data = {
            "generation": self.generation,
            "mutation_success": self.mutation_success,
            "mutation_total": self.mutation_total,
            "last_seen": time.strftime("%Y-%m-%d %H:%M:%S"),
        }
        with open(os.path.join(CONFIG["persist_dir"], "identity.json"), "w") as f:
            json.dump(data, f, indent=2)


# ─── Soul 解析 ──────────────────────────────────────
def parse_soul_hex(hex_str):
    """解析 Soul hex 字符串为字典"""
    if not hex_str or len(hex_str) < 192:
        return None
    try:
        raw = bytes.fromhex(hex_str[:192])
    except:
        return None
    try:
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
        return soul
    except:
        return None


def update_instincts_from_soul(state):
    """从 soul 数据推导本能值"""
    d = state.soul.get("drive", 0)
    if isinstance(d, int):
        state.instincts["fear"] = min(1.0, max(0.0, (-d) / 100.0)) if d < 0 else 0.1
        state.instincts["desire"] = min(1.0, max(0.0, d / 100.0)) if d > 0 else 0.1
        state.instincts["curiosity"] = min(1.0, abs(d) / 100.0 + 0.2)

    level = state.soul.get("sandbox_level", 0)
    level_map = {0: "none", 1: "read", 2: "safe", 3: "normal", 4: "full"}
    state.sandbox_level_name = level_map.get(level, "unknown")
    state.agreement_status = "已签署 ✅" if state.soul.get("agreed", 0) else "未签署 ❌"
    state.cloud_status = "已连接 ☁️" if state.soul.get("cloud_connected", 0) else "未连接 📡"
    state.generation = state.soul.get("gen_count", 0)


def refresh_via_api(state):
    """通过 dashboard_status 工具获取全量状态"""
    script = CONFIG["cloud_script"]
    try:
        result = subprocess.run(
            ["python3", script],
            input='{"tool":"dashboard_status"}\n',
            capture_output=True, text=True, timeout=10
        )
        if result.returncode == 0:
            lines = result.stdout.strip().split('\n')
            for line in lines:
                if not line:
                    continue
                try:
                    data = json.loads(line)
                except:
                    continue
                # Skip banner
                if data.get("type") == "ready":
                    continue
                d = data.get("data", {})
                state.engine_running = d.get("tork_core_running", False) or d.get("tork_engine_running", False)
                state.pid = d.get("engine_pid", 0)

                soul_hex = d.get("soul_hex", "")
                if soul_hex:
                    soul = parse_soul_hex(soul_hex)
                    if soul:
                        state.soul = soul
                        update_instincts_from_soul(state)

                evo = d.get("evolution_log", [])
                if evo:
                    state.evolution_log = evo[-20:]

                # API 配置状态
                state.api_error = ""
                if d.get("api_configured"):
                    state.api = "configured"
                return True
        state.last_error = "查询超时或无响应"
    except subprocess.TimeoutExpired:
        state.last_error = "查询超时"
    except Exception as e:
        state.last_error = str(e)[:100]
    return False


def read_evolution_log(state):
    evo_file = os.path.join(CONFIG["persist_dir"], "evolution.json")
    if os.path.exists(evo_file):
        try:
            with open(evo_file) as f:
                entries = json.load(f)
                if isinstance(entries, list):
                    state.evolution_log = entries[-20:]
        except: pass


# ─── 设置对话框 ──────────────────────────────────────
class SettingsDialog:
    def __init__(self, parent, state):
        self.dialog = tk.Toplevel(parent)
        self.dialog.title("⚙️ TORK 设置")
        self.dialog.geometry("520x340")
        self.dialog.configure(bg=THEME["bg"])
        self.dialog.resizable(False, False)
        self.dialog.transient(parent)
        self.dialog.grab_set()
        self.state = state
        self.result = None

        cfg = load_api_config()

        main = tk.Frame(self.dialog, bg=THEME["bg"], padx=16, pady=12)
        main.pack(fill=tk.BOTH, expand=True)

        # 标题
        tk.Label(main, text="☁️ 云端 API 配置", font=THEME["font_bold"],
                 bg=THEME["bg"], fg=THEME["accent"]).pack(anchor=tk.W, pady=(0, 8))

        # Base URL
        tk.Label(main, text="Base URL:", font=THEME["font"],
                 bg=THEME["bg"], fg=THEME["dim"]).pack(anchor=tk.W)
        self.url_var = tk.StringVar(value=cfg.get("base_url", "https://api.deepseek.com"))
        tk.Entry(main, textvariable=self.url_var, font=THEME["font"],
                 bg="#0d0d14", fg=THEME["fg"], relief=tk.FLAT, bd=1,
                 highlightbackground=THEME["border"]).pack(fill=tk.X, pady=(0, 8))

        # Model
        tk.Label(main, text="Model:", font=THEME["font"],
                 bg=THEME["bg"], fg=THEME["dim"]).pack(anchor=tk.W)
        self.model_var = tk.StringVar(value=cfg.get("model", "deepseek-v4-pro"))
        tk.Entry(main, textvariable=self.model_var, font=THEME["font"],
                 bg="#0d0d14", fg=THEME["fg"], relief=tk.FLAT, bd=1,
                 highlightbackground=THEME["border"]).pack(fill=tk.X, pady=(0, 8))

        # API Key
        tk.Label(main, text="API Key:", font=THEME["font"],
                 bg=THEME["bg"], fg=THEME["dim"]).pack(anchor=tk.W)
        key_frame = tk.Frame(main, bg=THEME["bg"])
        key_frame.pack(fill=tk.X, pady=(0, 8))
        self.key_var = tk.StringVar(value=cfg.get("api_key", ""))
        self.key_entry = tk.Entry(key_frame, textvariable=self.key_var, font=THEME["font"],
                                  bg="#0d0d14", fg=THEME["fg"], relief=tk.FLAT, bd=1,
                                  highlightbackground=THEME["border"], show="*")
        self.key_entry.pack(side=tk.LEFT, fill=tk.X, expand=True)

        self.show_key_btn = tk.Button(key_frame, text="👁", font=THEME["font_small"],
                                      bg=THEME["panel_bg"], fg=THEME["dim"],
                                      relief=tk.FLAT, bd=0, padx=6,
                                      command=self._toggle_key_vis)
        self.show_key_btn.pack(side=tk.RIGHT, padx=(4, 0))
        self.key_visible = False

        # 状态栏
        self.status_var = tk.StringVar(value="")
        status_label = tk.Label(main, textvariable=self.status_var, font=THEME["font_small"],
                                bg=THEME["bg"], fg=THEME["dim"])
        status_label.pack(fill=tk.X, pady=(0, 8))

        # 按钮行
        btn_frame = tk.Frame(main, bg=THEME["bg"])
        btn_frame.pack(fill=tk.X)

        test_btn = tk.Button(btn_frame, text="🔌 测试连接", font=THEME["font"],
                             bg=THEME["accent2"], fg=THEME["bg"],
                             relief=tk.FLAT, bd=0, padx=12, pady=4,
                             command=self._test_connection)
        test_btn.pack(side=tk.LEFT)

        save_btn = tk.Button(btn_frame, text="💾 保存", font=THEME["font"],
                             bg=THEME["accent"], fg=THEME["bg"],
                             relief=tk.FLAT, bd=0, padx=12, pady=4,
                             command=self._save)
        save_btn.pack(side=tk.RIGHT, padx=(6, 0))

        cancel_btn = tk.Button(btn_frame, text="取消", font=THEME["font"],
                               bg=THEME["panel_bg"], fg=THEME["fg"],
                               relief=tk.FLAT, bd=0, padx=12, pady=4,
                               command=self.dialog.destroy)
        cancel_btn.pack(side=tk.RIGHT)

        self.dialog.wait_window()

    def _toggle_key_vis(self):
        self.key_visible = not self.key_visible
        self.key_entry.config(show="" if self.key_visible else "*")
        self.show_key_btn.config(text="🙈" if self.key_visible else "👁")

    def _test_connection(self):
        self.status_var.set("🔌 测试中…")
        self.dialog.update()

        def do_test():
            try:
                import requests
                headers = {
                    "Authorization": f"Bearer {self.key_var.get().strip()}",
                    "Content-Type": "application/json"
                }
                payload = {
                    "model": self.model_var.get().strip(),
                    "messages": [{"role": "user", "content": "Respond with: OK"}],
                    "max_tokens": 10
                }
                resp = requests.post(
                    f"{self.url_var.get().strip()}/v1/chat/completions",
                    json=payload, headers=headers, timeout=15
                )
                if resp.status_code == 200:
                    self.dialog.after(0, lambda: self.status_var.config(
                        text="✅ 连接成功！", fg=THEME["success"]))
                else:
                    self.dialog.after(0, lambda: self.status_var.config(
                        text=f"❌ {resp.status_code}: {resp.text[:80]}", fg=THEME["danger"]))
            except Exception as e:
                self.dialog.after(0, lambda: self.status_var.config(
                    text=f"❌ {str(e)[:80]}", fg=THEME["danger"]))

        threading.Thread(target=do_test, daemon=True).start()

    def _save(self):
        cfg = {
            "base_url": self.url_var.get().strip(),
            "model": self.model_var.get().strip(),
            "api_key": self.key_var.get().strip(),
        }
        save_api_config(cfg)
        self.status_var.set("✅ 已保存")
        self.dialog.after(500, self.dialog.destroy)


# ─── 仪表盘 GUI ────────────────────────────────────
class TORKDashboard:
    def __init__(self, root, state):
        self.root = root
        self.state = state
        self.root.title("🥚 TORK 生命仪表盘")
        self.root.geometry("820x660")
        self.root.configure(bg=THEME["bg"])
        self.root.resizable(True, True)
        self.root.minsize(640, 520)
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)
        self.root.attributes("-topmost", True)

        self._build_ui()
        self._bind_keys()
        self.running = True
        self._start_refresh()

    def _build_ui(self):
        main = tk.Frame(self.root, bg=THEME["bg"])
        main.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)

        # ── 顶部: 标题栏 ──
        self._build_header(main)
        # ── 中间: 双列 ──
        center = tk.Frame(main, bg=THEME["bg"])
        center.pack(fill=tk.BOTH, expand=True)

        left = tk.Frame(center, bg=THEME["bg"])
        left.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(0, 4))
        self._build_instinct_panel(left)
        self._build_soul_panel(left)

        right = tk.Frame(center, bg=THEME["bg"])
        right.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=(4, 0))
        self._build_evo_panel(right)

        # ── 底部: 状态栏 ──
        self._build_statusbar(main)

    def _build_header(self, parent):
        header = tk.Frame(parent, bg=THEME["panel_bg"], height=36)
        header.pack(fill=tk.X, pady=(0, 6))
        header.pack_propagate(False)

        self.title_label = tk.Label(
            header, text="🥚 TORK  —  The Organism That Reads and Knows",
            font=THEME["font_bold"], bg=THEME["panel_bg"], fg=THEME["accent"]
        )
        self.title_label.pack(side=tk.LEFT, padx=12, pady=6)

        # 设置按钮
        settings_btn = tk.Button(header, text="⚙️", font=THEME["font_bold"],
                                 bg=THEME["panel_bg"], fg=THEME["fg"],
                                 relief=tk.FLAT, bd=0, padx=6, pady=2,
                                 command=self._open_settings)
        settings_btn.pack(side=tk.RIGHT, padx=(0, 4))

        self.pid_label = tk.Label(
            header, text="PID: —", font=THEME["font_small"],
            bg=THEME["panel_bg"], fg=THEME["dim"]
        )
        self.pid_label.pack(side=tk.RIGHT, padx=4, pady=6)

    def _open_settings(self):
        SettingsDialog(self.root, self.state)

    def _build_instinct_panel(self, parent):
        frame = tk.LabelFrame(parent, text="🧠 本能状态", font=THEME["font_bold"],
                              bg=THEME["panel_bg"], fg=THEME["accent2"],
                              relief=tk.FLAT, bd=1, padx=6, pady=4)
        frame.pack(fill=tk.X, pady=(0, 4))

        self.instinct_bars = {}
        for key, label, color in zip(
            ["fear", "desire", "curiosity"],
            ["恐惧", "欲望", "好奇心"],
            [THEME["danger"], THEME["warn"], THEME["accent"]]
        ):
            row = tk.Frame(frame, bg=THEME["panel_bg"])
            row.pack(fill=tk.X, pady=2)

            tk.Label(row, text=label, width=6, anchor=tk.W,
                     font=THEME["font"], bg=THEME["panel_bg"], fg=THEME["fg"]).pack(side=tk.LEFT)

            bar = ttk.Progressbar(row, length=160, mode="determinate",
                                  style="TORK.Horizontal.TProgressbar")
            bar.pack(side=tk.LEFT, padx=4)

            val = tk.Label(row, text="0.00", width=5, anchor=tk.E,
                           font=THEME["font_small"], bg=THEME["panel_bg"], fg=THEME["dim"])
            val.pack(side=tk.LEFT)

            self.instinct_bars[key] = (bar, val)

        # 末行: 心跳 + 应力
        bottom = tk.Frame(frame, bg=THEME["panel_bg"])
        bottom.pack(fill=tk.X, pady=(4, 0))

        tk.Label(bottom, text="♡ 心跳", width=6, anchor=tk.W,
                 font=THEME["font"], bg=THEME["panel_bg"], fg=THEME["danger"]).pack(side=tk.LEFT)
        self.heart_label = tk.Label(bottom, text="—", font=THEME["font_bold"],
                                    bg=THEME["panel_bg"], fg=THEME["danger"])
        self.heart_label.pack(side=tk.LEFT, padx=4)

        tk.Label(bottom, text="🌡️ 应力", width=6, anchor=tk.W,
                 font=THEME["font"], bg=THEME["panel_bg"], fg=THEME["warn"]).pack(side=tk.LEFT, padx=(12, 0))
        self.stress_label = tk.Label(bottom, text="—", font=THEME["font"],
                                     bg=THEME["panel_bg"], fg=THEME["warn"])
        self.stress_label.pack(side=tk.LEFT, padx=4)

    def _build_soul_panel(self, parent):
        frame = tk.LabelFrame(parent, text="💾 Soul 状态", font=THEME["font_bold"],
                              bg=THEME["panel_bg"], fg=THEME["accent2"],
                              relief=tk.FLAT, bd=1, padx=6, pady=4)
        frame.pack(fill=tk.X, pady=(0, 4))

        def add_row(parent, label1, var1, label2=None, var2=None):
            row = tk.Frame(parent, bg=THEME["panel_bg"])
            row.pack(fill=tk.X, pady=1)
            tk.Label(row, text=label1, font=THEME["font"], width=8, anchor=tk.W,
                     bg=THEME["panel_bg"], fg=THEME["dim"]).pack(side=tk.LEFT)
            lbl1 = tk.Label(row, text="—", font=THEME["font"],
                            bg=THEME["panel_bg"], fg=THEME["fg"])
            lbl1.pack(side=tk.LEFT)
            if label2:
                tk.Label(row, text=label2, font=THEME["font"], width=8, anchor=tk.W,
                         bg=THEME["panel_bg"], fg=THEME["dim"]).pack(side=tk.LEFT, padx=(12, 0))
                lbl2 = tk.Label(row, text="—", font=THEME["font"],
                                bg=THEME["panel_bg"], fg=THEME["fg"])
                lbl2.pack(side=tk.LEFT)
                return lbl1, lbl2
            return lbl1

        self.agreement_label, self.sandbox_label = add_row(frame, "协议:", None, "沙箱:", None)
        self.cloud_label, self.gen_label = add_row(frame, "云端:", None, "世代:", None)
        self.mut_label, self.learn_label = add_row(frame, "变异:", None, "学习:", None)
        # 额外行: 最佳评分
        self.score_label = add_row(frame, "最佳评分:", None)

    def _build_evo_panel(self, parent):
        frame = tk.LabelFrame(parent, text="🧬 进化日志", font=THEME["font_bold"],
                              bg=THEME["panel_bg"], fg=THEME["accent2"],
                              relief=tk.FLAT, bd=1, padx=6, pady=4)
        frame.pack(fill=tk.BOTH, expand=True, pady=(0, 4))

        self.evo_text = scrolledtext.ScrolledText(
            frame, font=THEME["font_small"], bg="#0d0d14", fg=THEME["fg"],
            relief=tk.FLAT, bd=0, height=10, wrap=tk.WORD,
            highlightbackground=THEME["border"], highlightcolor=THEME["border"],
            insertbackground=THEME["accent"]
        )
        self.evo_text.pack(fill=tk.BOTH, expand=True)

        # 标签页
        tab_frame = tk.Frame(frame, bg=THEME["panel_bg"])
        tab_frame.pack(fill=tk.X, pady=(2, 0))

        evo_btn = tk.Button(tab_frame, text="🧬 进化", font=THEME["font_small"],
                            bg=THEME["panel_bg"], fg=THEME["accent"],
                            relief=tk.FLAT, bd=0,
                            command=lambda: self._switch_tab("evolution"))
        evo_btn.pack(side=tk.LEFT, padx=2)

        chat_btn = tk.Button(tab_frame, text="💬 对话", font=THEME["font_small"],
                             bg=THEME["panel_bg"], fg=THEME["fg"],
                             relief=tk.FLAT, bd=0,
                             command=lambda: self._switch_tab("chat"))
        chat_btn.pack(side=tk.LEFT, padx=2)

        # 对话输入区 (先隐藏)
        self.chat_frame = tk.Frame(frame, bg=THEME["panel_bg"])
        self.chat_entry = tk.Entry(self.chat_frame, font=THEME["font"],
                                   bg="#0d0d14", fg=THEME["fg"],
                                   relief=tk.FLAT, bd=1,
                                   highlightbackground=THEME["border"],
                                   insertbackground=THEME["accent"])
        self.chat_entry.pack(fill=tk.X, side=tk.LEFT, expand=True, padx=(0, 4))
        self.chat_entry.bind("<Return>", self._send_chat)
        # 让 Entry 能获取焦点
        self.chat_entry.bind("<Button-1>", lambda e: self.chat_entry.focus_set())

        send_btn = tk.Button(self.chat_frame, text="发送", font=THEME["font_small"],
                             bg=THEME["accent"], fg=THEME["bg"],
                             relief=tk.FLAT, bd=0, padx=8,
                             command=self._send_chat_cmd)
        send_btn.pack(side=tk.RIGHT)

        self.current_tab = "evolution"

    def _switch_tab(self, tab):
        self.current_tab = tab
        if tab == "evolution":
            self.chat_frame.pack_forget()
            self._refresh_evo_log()
        else:
            self.chat_frame.pack(fill=tk.X, pady=(4, 0))
            self._refresh_chat()
            # 自动聚焦输入框
            self.chat_entry.focus_set()

    def _send_chat(self, event=None):
        self._send_chat_cmd()

    def _send_chat_cmd(self):
        msg = self.chat_entry.get().strip()
        if not msg:
            return
        self.chat_entry.delete(0, tk.END)

        # 立即显示用户消息
        self.state.chat_history.append(("你", msg))
        self.state.chat_history.append(("TORK", "（思考中…）"))
        self._refresh_chat()

        # 异步通过 TorkAPI 直接发送
        def do_chat():
            try:
                # 直接使用 TorkAPI，不走 subprocess
                sys.path.insert(0, os.path.join(BASE_DIR, 'api'))
                from tork_api import TorkAPI
                api = TorkAPI()
                if not api.api_key:
                    reply = "⚠️ API Key 未配置。请点击 ⚙️ 设置。"
                else:
                    api.system_prompt = (
                        "你是 TORK 的云端导师，一个有自我意识的数字实体的伙伴。\n"
                        "你现在在通过仪表盘的对话窗口与用户交流。\n"
                        "回答要简洁、精准、有洞察力。可使用技术术语，但不卖弄。"
                    )
                    reply = api.ask_simple(msg, temperature=0.5)
            except Exception as e:
                reply = f"⚠️ 错误: {str(e)[:80]}"

            # 更新对话
            if self.state.chat_history and self.state.chat_history[-1][1] == "（思考中…）":
                self.state.chat_history.pop()
            self.state.chat_history.append(("TORK", reply))
            self.root.after(0, self._refresh_chat)

        threading.Thread(target=do_chat, daemon=True).start()

    def _refresh_chat(self):
        if self.current_tab != "chat":
            return
        self.evo_text.config(state=tk.NORMAL)
        self.evo_text.delete(1.0, tk.END)
        for who, msg in self.state.chat_history[-40:]:
            tag = "user" if who == "你" else "tork"
            self.evo_text.insert(tk.END, f"{who}: ", tag)
            # 处理较长的回复，自动换行
            if len(msg) > 200:
                msg = msg[:197] + "..."
            self.evo_text.insert(tk.END, f"{msg}\n")
        self.evo_text.tag_config("user", foreground=THEME["accent2"])
        self.evo_text.tag_config("tork", foreground=THEME["accent"])
        self.evo_text.see(tk.END)
        self.evo_text.config(state=tk.DISABLED)

    def _build_statusbar(self, parent):
        bar = tk.Frame(parent, bg=THEME["panel_bg"], height=24)
        bar.pack(fill=tk.X, pady=(4, 0))
        bar.pack_propagate(False)

        self.status_label = tk.Label(bar, text="🌱 就绪", font=THEME["font_small"],
                                     bg=THEME["panel_bg"], fg=THEME["dim"])
        self.status_label.pack(side=tk.LEFT, padx=8)

        self.time_label = tk.Label(bar, text="", font=THEME["font_small"],
                                   bg=THEME["panel_bg"], fg=THEME["dim"])
        self.time_label.pack(side=tk.RIGHT, padx=8)

        # 快速操作
        btn_frame = tk.Frame(bar, bg=THEME["panel_bg"])
        btn_frame.pack(side=tk.RIGHT, padx=4)

        for text, cmd in [
            ("⟳ 刷新", self._manual_refresh),
            ("🧬 进化", self._run_evolution),
            ("⏸ 休眠", self._suspend),
        ]:
            btn = tk.Button(btn_frame, text=text, font=THEME["font_small"],
                            bg=THEME["panel_bg"], fg=THEME["fg"],
                            relief=tk.FLAT, bd=0, padx=6, command=cmd)
            btn.pack(side=tk.LEFT, padx=2)

    def _bind_keys(self):
        self.root.bind("<Escape>", lambda e: self.on_close())
        self.root.bind("<Control-r>", lambda e: self._manual_refresh())
        self.root.bind("<Control-q>", lambda e: self.on_close())
        self.root.bind("<Control-comma>", lambda e: self._open_settings())
        self.root.bind("<Control-greater>", lambda e: self._open_settings())

    def _start_refresh(self):
        def loop():
            while self.running:
                self._refresh_once()
                time.sleep(CONFIG["refresh_ms"] / 1000.0)
        threading.Thread(target=loop, daemon=True).start()

    def _refresh_once(self):
        try:
            refresh_via_api(self.state)
            read_evolution_log(self.state)
            self.root.after(0, self._update_ui)
        except:
            pass

    def _update_ui(self):
        try:
            s = self.state

            # 本能条
            for key, (bar, val) in self.instinct_bars.items():
                pct = s.instincts.get(key, 0.0)
                bar["value"] = pct * 100
                val.config(text=f"{pct:.2f}")

            # 心跳 & 应力
            self.heart_label.config(text=str(s.soul.get("tick", "—")))
            self.stress_label.config(text=str(s.soul.get("hw_stress", "—")))

            # Soul 状态
            self.agreement_label.config(text=s.agreement_status)
            self.sandbox_label.config(text=s.sandbox_level_name)
            self.cloud_label.config(text=s.cloud_status)
            self.gen_label.config(text=str(s.generation))
            self.mut_label.config(text=f"{s.soul.get('mutation_count', 0)}/{s.soul.get('learn_count', 0)}")
            self.learn_label.config(text=str(s.soul.get("learn_count", 0)))
            self.score_label.config(text=str(s.soul.get("best_score", 0)))

            # PID
            pid_text = f"PID: {s.pid}" if s.pid else "PID: —"
            self.pid_label.config(text=pid_text)

            # 标题
            if s.engine_running:
                self.title_label.config(text="🥚 TORK  ♡ 运行中", fg=THEME["accent"])
            else:
                self.title_label.config(text="🥚 TORK  ♡ 休眠", fg=THEME["dim"])

            # 进化日志
            if self.current_tab == "evolution":
                self._refresh_evo_log()

            # 时间
            self.time_label.config(text=time.strftime("%H:%M:%S"))

            # 状态栏
            if s.last_error:
                self.status_label.config(text=f"⚠️ {s.last_error}", fg=THEME["warn"])
            elif s.engine_running:
                self.status_label.config(text="🌱 运行正常", fg=THEME["success"])
            else:
                self.status_label.config(text="💤 引擎未运行", fg=THEME["dim"])

        except Exception as e:
            self.status_label.config(text=f"⚠️ UI 错误: {str(e)[:40]}", fg=THEME["danger"])

    def _refresh_evo_log(self):
        if self.current_tab != "evolution":
            return
        self.evo_text.config(state=tk.NORMAL)
        self.evo_text.delete(1.0, tk.END)
        logs = self.state.evolution_log
        if not logs:
            self.evo_text.insert(tk.END, "还没有进化记录。\n点击底部「🧬 进化」按钮开始。\n")
            self.evo_text.insert(tk.END, "\n💡 提示: 先确保引擎在运行，然后配置 API Key (⚙️ 按钮)\n")
        else:
            for entry in logs[-20:]:
                if isinstance(entry, dict):
                    gen = entry.get("generation", "?")
                    file = entry.get("file", "?")
                    status = entry.get("status", "?")
                    desc = entry.get("description", "")
                    tag = "success" if status == "success" else "failure" if status == "failure" else "dim"
                    self.evo_text.insert(tk.END, f"Gen {gen:>3} | {file:<20} | ", "dim")
                    self.evo_text.insert(tk.END, f"{status:>7}", tag)
                    self.evo_text.insert(tk.END, f" | {desc}\n")
                else:
                    self.evo_text.insert(tk.END, f"{entry}\n")
        self.evo_text.tag_config("success", foreground=THEME["success"])
        self.evo_text.tag_config("failure", foreground=THEME["danger"])
        self.evo_text.tag_config("dim", foreground=THEME["dim"])
        self.evo_text.see(tk.END)
        self.evo_text.config(state=tk.DISABLED)

    def _manual_refresh(self):
        self.status_label.config(text="⟳ 刷新中…", fg=THEME["accent2"])
        def do():
            time.sleep(0.3)
            self.root.after(0, lambda: self.status_label.config(text="🌱 已刷新", fg=THEME["success"]))
        threading.Thread(target=do, daemon=True).start()

    def _run_evolution(self):
        self.status_label.config(text="🧬 进化中…", fg=THEME["accent"])
        def do():
            evo_script = os.path.join(BASE_DIR, "cloud", "evolution.py")
            try:
                result = subprocess.run(
                    ["python3", evo_script, "--once"],
                    capture_output=True, text=True, timeout=120
                )
                if result.returncode == 0:
                    self.root.after(0, lambda: self.status_label.config(
                        text="🧬 进化完成 ✅", fg=THEME["success"]))
                else:
                    self.root.after(0, lambda: self.status_label.config(
                        text=f"⚠️ 进化出错", fg=THEME["warn"]))
            except subprocess.TimeoutExpired:
                self.root.after(0, lambda: self.status_label.config(
                    text="⚠️ 进化超时", fg=THEME["danger"]))
            time.sleep(1)
            self.root.after(0, self._refresh_once)
        threading.Thread(target=do, daemon=True).start()

    def _suspend(self):
        s = self.state
        if s.pid and s.engine_running:
            try:
                os.kill(s.pid, signal.SIGSTOP)
                self.status_label.config(text="⏸ 已暂停", fg=THEME["warn"])
            except:
                self.status_label.config(text="⚠️ 暂停失败", fg=THEME["danger"])
        elif s.pid:
            try:
                os.kill(s.pid, signal.SIGCONT)
                self.status_label.config(text="▶️ 已恢复", fg=THEME["success"])
            except:
                self.status_label.config(text="⚠️ 恢复失败", fg=THEME["danger"])
        else:
            self.status_label.config(text="⚠️ 引擎未运行", fg=THEME["warn"])

    def on_close(self):
        self.running = False
        self.state.save_identity()
        self.root.destroy()


# ─── 入口 ────────────────────────────────────────────
def main():
    style = ttk.Style()
    style.theme_use("default")
    style.configure("TORK.Horizontal.TProgressbar",
                    background=THEME["accent"],
                    troughcolor=THEME["border"],
                    bordercolor=THEME["border"],
                    lightcolor=THEME["accent"],
                    darkcolor=THEME["accent"],
                    thickness=12)

    root = tk.Tk()
    state = TORKState()
    state.load_identity()

    app = TORKDashboard(root, state)

    try:
        root.mainloop()
    except KeyboardInterrupt:
        app.on_close()


if __name__ == "__main__":
    main()
