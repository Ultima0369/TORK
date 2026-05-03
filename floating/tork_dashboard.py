#!/usr/bin/env python3
"""
TORK 生命仪表盘
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
显示心跳、本能、进化状态、云端连接
通过 Cloud Protocol 与 TORK 后端通信
"""

import tkinter as tk
from tkinter import ttk, scrolledtext
import subprocess
import json
import os
import time
import threading
import sys
import signal

# ─── 配置 ────────────────────────────────────────────
CONFIG = {
    "refresh_ms": 1000,         # 刷新间隔 (毫秒)
    "cloud_script": os.path.join(os.path.dirname(__file__), "..", "cloud", "cloud_protocol.py"),
    "persist_dir": os.path.join(os.path.dirname(__file__), "..", "persist"),
    "soul_layout": {
        "tick":         {"offset": 0x00, "type": "uint32"},
        "last_tsc":     {"offset": 0x04, "type": "uint64"},
        "cur_tsc":      {"offset": 0x0C, "type": "uint64"},
        "elapsed":      {"offset": 0x14, "type": "uint64"},
        "expected":     {"offset": 0x1C, "type": "uint64"},
        "hw_stress":    {"offset": 0x24, "type": "uint8"},
        "mode":         {"offset": 0x25, "type": "uint8"},
        "crc":          {"offset": 0x28, "type": "uint32"},
        "self_pid":     {"offset": 0x2C, "type": "uint32"},
        "drive":        {"offset": 0x30, "type": "int8"},
        "ppid":         {"offset": 0x32, "type": "uint16"},
        "code_insns":   {"offset": 0x34, "type": "uint16"},
        "code_mov":     {"offset": 0x36, "type": "uint16"},
        "code_arith":   {"offset": 0x38, "type": "uint16"},
        "code_ctrl":    {"offset": 0x3A, "type": "uint16"},
        "code_other":   {"offset": 0x3C, "type": "uint16"},
        "mod_success":  {"offset": 0x3E, "type": "uint8"},
        "opt_saved":    {"offset": 0x3F, "type": "uint8"},
        "nop_count":    {"offset": 0x40, "type": "uint8"},
        "fission_count":{"offset": 0x41, "type": "uint8"},
        "child_pid":    {"offset": 0x42, "type": "uint16"},
        "fission_tick": {"offset": 0x44, "type": "uint16"},
        "wins":         {"offset": 0x46, "type": "uint16"},
        "agreed":       {"offset": 0x48, "type": "uint8"},
        "sandbox_level":{"offset": 0x49, "type": "uint8"},
        "cloud_connected":{"offset": 0x4A, "type": "uint8"},
        "cloud_provider":{"offset": 0x4B, "type": "uint8"},
        "learn_count":  {"offset": 0x4C, "type": "uint16"},
        "mutation_count":{"offset": 0x4E, "type": "uint16"},
        "best_score":   {"offset": 0x50, "type": "uint32"},
        "gen_count":    {"offset": 0x54, "type": "uint32"},
    },
}

# ─── 颜色主题 ────────────────────────────────────────
THEME = {
    "bg": "#0a0a0f",
    "fg": "#c0c8d0",
    "accent": "#4ec9b0",
    "accent2": "#569cd6",
    "warn": "#dcdcaa",
    "danger": "#f44747",
    "success": "#6a9955",
    "dim": "#606060",
    "panel_bg": "#13131a",
    "border": "#1e1e2e",
    "font": ("Consolas", 10),
    "font_bold": ("Consolas", 10, "bold"),
    "font_small": ("Consolas", 8),
    "font_large": ("Consolas", 14, "bold"),
}

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
        self.instinct_labels = ["恐惧", "欲望", "好奇心"]
        self.last_error = ""
        self.agreement_status = "unknown"
        self.sandbox_level_name = "none"

    def load_identity(self):
        """从 persist/ 加载持久化身份"""
        identity_file = os.path.join(CONFIG["persist_dir"], "identity.json")
        if os.path.exists(identity_file):
            try:
                with open(identity_file) as f:
                    data = json.load(f)
                    self.generation = data.get("generation", 0)
                    self.mutation_success = data.get("mutation_success", 0)
                    self.mutation_total = data.get("mutation_total", 0)
            except: pass

    def save_identity(self):
        """保存持久化身份"""
        identity_file = os.path.join(CONFIG["persist_dir"], "identity.json")
        os.makedirs(CONFIG["persist_dir"], exist_ok=True)
        data = {
            "generation": self.generation,
            "mutation_success": self.mutation_success,
            "mutation_total": self.mutation_total,
            "last_seen": time.strftime("%Y-%m-%d %H:%M:%S"),
        }
        with open(identity_file, "w") as f:
            json.dump(data, f, indent=2)


def update_state_from_soul(state, raw_soul):
    """解析 Soul 二进制数据到状态对象"""
    try:
        hex_str = raw_soul.strip()
        if len(hex_str) < 192:  # 96 bytes * 2 hex chars
            return False
        raw = bytes.fromhex(hex_str[:192])
    except:
        return False

    layout = CONFIG["soul_layout"]
    try:
        import struct
        # 解析完整 v2.0 Soul
        state.soul["tick"]           = struct.unpack_from("<I", raw, 0x00)[0]
        state.soul["hw_stress"]      = raw[0x24]
        state.soul["drive"]          = struct.unpack_from("<b", raw, 0x30)[0]
        state.soul["agreed"]         = raw[0x48]
        state.soul["sandbox_level"]  = raw[0x49]
        state.soul["cloud_connected"]= raw[0x4A]
        state.soul["cloud_provider"] = raw[0x4B]
        state.soul["learn_count"]    = struct.unpack_from("<H", raw, 0x4C)[0]
        state.soul["mutation_count"] = struct.unpack_from("<H", raw, 0x4E)[0]
        state.soul["best_score"]     = struct.unpack_from("<I", raw, 0x50)[0]
        state.soul["gen_count"]      = struct.unpack_from("<I", raw, 0x54)[0]

        # 推导本能值 (从 drive 分解: drive 是 int8, -128..+127)
        # 负 drive = 恐惧/减速, 正 drive = 欲望/加速, 绝对值 = 好奇心强度
        d = state.soul.get("drive", 0)
        state.instincts["fear"] = min(1.0, max(0.0, (-d) / 100.0)) if d < 0 else 0.1
        state.instincts["desire"] = min(1.0, max(0.0, d / 100.0)) if d > 0 else 0.1
        state.instincts["curiosity"] = min(1.0, abs(d) / 100.0 + 0.2)

        # 状态映射
        level = state.soul.get("sandbox_level", 0)
        level_map = {0: "none", 1: "read", 2: "safe", 3: "normal", 4: "full"}
        state.sandbox_level_name = level_map.get(level, "unknown")
        state.agreement_status = "已签署 ✅" if state.soul.get("agreed", 0) else "未签署 ❌"
        state.cloud_status = "已连接 ☁️" if state.soul.get("cloud_connected", 0) else "未连接 📡"

        state.generation = state.soul.get("gen_count", 0)
        return True
    except:
        return False


def query_cloud(state):
    """通过 Cloud Protocol 查询 TORK 状态"""
    script = CONFIG["cloud_script"]
    try:
        result = subprocess.run(
            ["python3", script, "status"],
            capture_output=True, text=True, timeout=10
        )
        if result.returncode == 0:
            data = json.loads(result.stdout.strip())
            state.engine_running = data.get("engine_running", False)
            state.pid = data.get("pid", 0)
            raw_soul = data.get("soul", "")
            if raw_soul:
                update_state_from_soul(state, raw_soul)
            return True
        else:
            state.last_error = result.stderr.strip()[:100]
            return False
    except subprocess.TimeoutExpired:
        state.last_error = "查询超时"
        return False
    except Exception as e:
        state.last_error = str(e)[:100]
        return False


def read_evolution_log(state):
    """读取进化日志"""
    evo_file = os.path.join(CONFIG["persist_dir"], "evolution.json")
    if os.path.exists(evo_file):
        try:
            with open(evo_file) as f:
                entries = json.load(f)
                if isinstance(entries, list):
                    state.evolution_log = entries[-20:]  # 最近20条
        except: pass


# ─── 仪表盘 GUI ────────────────────────────────────
class TORKDashboard:
    def __init__(self, root, state):
        self.root = root
        self.state = state
        self.root.title("🥚 TORK 生命仪表盘")
        self.root.geometry("780x620")
        self.root.configure(bg=THEME["bg"])
        self.root.resizable(True, True)
        self.root.minsize(600, 480)

        # 设置图标 (简单的 ASCII art)
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

        # 让窗口可置顶
        self.root.attributes("-topmost", True)

        self._build_ui()
        self._bind_keys()
        self.running = True

        # 启动刷新线程
        self._start_refresh()

    def _build_ui(self):
        """构建界面"""
        # 主框架
        main = tk.Frame(self.root, bg=THEME["bg"])
        main.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)

        # ── 顶部: 标题栏 ──
        header = tk.Frame(main, bg=THEME["panel_bg"], height=36)
        header.pack(fill=tk.X, pady=(0, 6))
        header.pack_propagate(False)

        self.title_label = tk.Label(
            header, text="🥚 TORK  —  The Organism That Reads and Knows",
            font=THEME["font_bold"], bg=THEME["panel_bg"], fg=THEME["accent"]
        )
        self.title_label.pack(side=tk.LEFT, padx=12, pady=6)

        self.pid_label = tk.Label(
            header, text="PID: —", font=THEME["font_small"],
            bg=THEME["panel_bg"], fg=THEME["dim"]
        )
        self.pid_label.pack(side=tk.RIGHT, padx=12, pady=6)

        # ── 中间: 双列布局 ──
        center = tk.Frame(main, bg=THEME["bg"])
        center.pack(fill=tk.BOTH, expand=True)

        # 左列: 本能 + Soul
        left = tk.Frame(center, bg=THEME["bg"])
        left.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(0, 4))

        # 本能面板
        self._build_instinct_panel(left)

        # Soul 状态面板
        self._build_soul_panel(left)

        # 右列: 进化日志 + 对话
        right = tk.Frame(center, bg=THEME["bg"])
        right.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=(4, 0))

        # 进化日志
        self._build_evo_panel(right)

        # ── 底部: 状态栏 ──
        self._build_statusbar(main)

    def _build_instinct_panel(self, parent):
        """本能面板"""
        frame = tk.LabelFrame(parent, text="🧠 本能状态", font=THEME["font_bold"],
                              bg=THEME["panel_bg"], fg=THEME["accent2"],
                              relief=tk.FLAT, bd=1, padx=6, pady=4)
        frame.pack(fill=tk.X, pady=(0, 4))

        self.instinct_bars = {}
        colors = [THEME["danger"], THEME["warn"], THEME["accent"]]
        for i, (key, label) in enumerate(zip(
            ["fear", "desire", "curiosity"],
            ["恐惧", "欲望", "好奇心"]
        )):
            row = tk.Frame(frame, bg=THEME["panel_bg"])
            row.pack(fill=tk.X, pady=2)

            lbl = tk.Label(row, text=label, width=6, anchor=tk.W,
                           font=THEME["font"], bg=THEME["panel_bg"], fg=THEME["fg"])
            lbl.pack(side=tk.LEFT)

            # 进度条
            bar = ttk.Progressbar(row, length=160, mode="determinate",
                                  style="TORK.Horizontal.TProgressbar")
            bar.pack(side=tk.LEFT, padx=4)

            # 数值标签
            val = tk.Label(row, text="0.00", width=5, anchor=tk.E,
                           font=THEME["font_small"], bg=THEME["panel_bg"], fg=THEME["dim"])
            val.pack(side=tk.LEFT)

            self.instinct_bars[key] = (bar, val)

        # 心跳
        heart_frame = tk.Frame(frame, bg=THEME["panel_bg"])
        heart_frame.pack(fill=tk.X, pady=(4, 0))
        tk.Label(heart_frame, text="♡ 心跳 (tick)", width=6, anchor=tk.W,
                 font=THEME["font"], bg=THEME["panel_bg"], fg=THEME["danger"]).pack(side=tk.LEFT)
        self.heart_label = tk.Label(heart_frame, text="—", font=THEME["font_bold"],
                                    bg=THEME["panel_bg"], fg=THEME["danger"])
        self.heart_label.pack(side=tk.LEFT, padx=4)

        # 温度
        tk.Label(heart_frame, text="🌡️ 应力", width=6, anchor=tk.W,
                 font=THEME["font"], bg=THEME["panel_bg"], fg=THEME["warn"]).pack(side=tk.LEFT, padx=(12,0))
        self.temp_label = tk.Label(heart_frame, text="—", font=THEME["font"],
                                   bg=THEME["panel_bg"], fg=THEME["warn"])
        self.temp_label.pack(side=tk.LEFT, padx=4)

    def _build_soul_panel(self, parent):
        """Soul 状态面板"""
        frame = tk.LabelFrame(parent, text="💾 Soul 状态", font=THEME["font_bold"],
                              bg=THEME["panel_bg"], fg=THEME["accent2"],
                              relief=tk.FLAT, bd=1, padx=6, pady=4)
        frame.pack(fill=tk.X, pady=(0, 4))

        # 协议状态
        row1 = tk.Frame(frame, bg=THEME["panel_bg"])
        row1.pack(fill=tk.X, pady=1)
        tk.Label(row1, text="协议:", font=THEME["font"], width=8, anchor=tk.W,
                 bg=THEME["panel_bg"], fg=THEME["dim"]).pack(side=tk.LEFT)
        self.agreement_label = tk.Label(row1, text="未知", font=THEME["font"],
                                        bg=THEME["panel_bg"], fg=THEME["fg"])
        self.agreement_label.pack(side=tk.LEFT)

        tk.Label(row1, text="沙箱:", font=THEME["font"], width=8, anchor=tk.W,
                 bg=THEME["panel_bg"], fg=THEME["dim"]).pack(side=tk.LEFT, padx=(12,0))
        self.sandbox_label = tk.Label(row1, text="—", font=THEME["font"],
                                      bg=THEME["panel_bg"], fg=THEME["fg"])
        self.sandbox_label.pack(side=tk.LEFT)

        # 云端状态
        row2 = tk.Frame(frame, bg=THEME["panel_bg"])
        row2.pack(fill=tk.X, pady=1)
        tk.Label(row2, text="云端:", font=THEME["font"], width=8, anchor=tk.W,
                 bg=THEME["panel_bg"], fg=THEME["dim"]).pack(side=tk.LEFT)
        self.cloud_label = tk.Label(row2, text="未连接 📡", font=THEME["font"],
                                    bg=THEME["panel_bg"], fg=THEME["dim"])
        self.cloud_label.pack(side=tk.LEFT)

        tk.Label(row2, text="世代:", font=THEME["font"], width=8, anchor=tk.W,
                 bg=THEME["panel_bg"], fg=THEME["dim"]).pack(side=tk.LEFT, padx=(12,0))
        self.gen_label = tk.Label(row2, text="0", font=THEME["font_bold"],
                                  bg=THEME["panel_bg"], fg=THEME["accent"])
        self.gen_label.pack(side=tk.LEFT)

        # 变异统计
        row3 = tk.Frame(frame, bg=THEME["panel_bg"])
        row3.pack(fill=tk.X, pady=1)
        tk.Label(row3, text="变异:", font=THEME["font"], width=8, anchor=tk.W,
                 bg=THEME["panel_bg"], fg=THEME["dim"]).pack(side=tk.LEFT)
        self.mut_label = tk.Label(row3, text="0/0", font=THEME["font"],
                                  bg=THEME["panel_bg"], fg=THEME["fg"])
        self.mut_label.pack(side=tk.LEFT)

        tk.Label(row3, text="学习:", font=THEME["font"], width=8, anchor=tk.W,
                 bg=THEME["panel_bg"], fg=THEME["dim"]).pack(side=tk.LEFT, padx=(12,0))
        self.learn_label = tk.Label(row3, text="0", font=THEME["font"],
                                    bg=THEME["panel_bg"], fg=THEME["fg"])
        self.learn_label.pack(side=tk.LEFT)

    def _build_evo_panel(self, parent):
        """进化日志面板"""
        frame = tk.LabelFrame(parent, text="🧬 进化日志", font=THEME["font_bold"],
                              bg=THEME["panel_bg"], fg=THEME["accent2"],
                              relief=tk.FLAT, bd=1, padx=6, pady=4)
        frame.pack(fill=tk.BOTH, expand=True, pady=(0, 4))

        self.evo_text = scrolledtext.ScrolledText(
            frame, font=THEME["font_small"], bg="#0d0d14", fg=THEME["fg"],
            relief=tk.FLAT, bd=0, height=8, wrap=tk.WORD,
            highlightbackground=THEME["border"], highlightcolor=THEME["border"],
            insertbackground=THEME["accent"]
        )
        self.evo_text.pack(fill=tk.BOTH, expand=True)

        # 标签页: 进化 | 对话
        tab_frame = tk.Frame(frame, bg=THEME["panel_bg"])
        tab_frame.pack(fill=tk.X, pady=(2, 0))

        self.tab_var = tk.StringVar(value="evolution")
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

        # 对话输入 (隐藏)
        self.chat_frame = tk.Frame(frame, bg=THEME["panel_bg"])
        self.chat_entry = tk.Entry(self.chat_frame, font=THEME["font"],
                                   bg="#0d0d14", fg=THEME["fg"],
                                   relief=tk.FLAT, bd=1,
                                   highlightbackground=THEME["border"],
                                   insertbackground=THEME["accent"])
        self.chat_entry.pack(fill=tk.X, side=tk.LEFT, expand=True, padx=(0, 4))
        self.chat_entry.bind("<Return>", self._send_chat)

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

    def _send_chat(self, event=None):
        self._send_chat_cmd()

    def _send_chat_cmd(self):
        msg = self.chat_entry.get().strip()
        if not msg:
            return
        self.chat_entry.delete(0, tk.END)
        # 显示在对话区
        self.state.chat_history.append(("你", msg))
        self.state.chat_history.append(("TORK", "（思考中…）"))
        self._refresh_chat()

        # 通过云端协议发送 (异步)
        def do_chat():
            script = CONFIG["cloud_script"]
            import subprocess, json
            try:
                payload = json.dumps({"tool": "ask_deepseek", "args": {"prompt": msg}})
                result = subprocess.run(
                    ["python3", script, "ask", msg],
                    capture_output=True, text=True, timeout=30
                )
                if result.returncode == 0:
                    data = json.loads(result.stdout.strip())
                    reply = data.get("response", "（无响应）")
                else:
                    reply = f"（错误: {result.stderr.strip()[:80]}）"
            except Exception as e:
                reply = f"（错误: {str(e)[:80]}）"

            # 更新最后一条
            if self.state.chat_history and self.state.chat_history[-1][1] == "（思考中…）":
                self.state.chat_history.pop()
            self.state.chat_history.append(("TORK", reply))
            self.root.after(0, self._refresh_chat)

        threading.Thread(target=do_chat, daemon=True).start()

    def _refresh_chat(self):
        """刷新对话区域 (在 tab 切换时调用)"""
        if self.current_tab != "chat":
            return
        self.evo_text.config(state=tk.NORMAL)
        self.evo_text.delete(1.0, tk.END)
        for who, msg in self.state.chat_history[-30:]:
            tag = "user" if who == "你" else "tork"
            self.evo_text.insert(tk.END, f"{who}: ", tag)
            self.evo_text.insert(tk.END, f"{msg}\n")
        self.evo_text.tag_config("user", foreground=THEME["accent2"])
        self.evo_text.tag_config("tork", foreground=THEME["accent"])
        self.evo_text.see(tk.END)
        self.evo_text.config(state=tk.DISABLED)

    def _build_statusbar(self, parent):
        """状态栏"""
        bar = tk.Frame(parent, bg=THEME["panel_bg"], height=24)
        bar.pack(fill=tk.X, pady=(4, 0))
        bar.pack_propagate(False)

        self.status_label = tk.Label(bar, text="🌱 就绪", font=THEME["font_small"],
                                     bg=THEME["panel_bg"], fg=THEME["dim"])
        self.status_label.pack(side=tk.LEFT, padx=8)

        self.time_label = tk.Label(bar, text="", font=THEME["font_small"],
                                   bg=THEME["panel_bg"], fg=THEME["dim"])
        self.time_label.pack(side=tk.RIGHT, padx=8)

        # 快速操作按钮
        btn_frame = tk.Frame(bar, bg=THEME["panel_bg"])
        btn_frame.pack(side=tk.RIGHT, padx=4)

        actions = [
            ("⟳ 刷新", self._manual_refresh),
            ("🧬 进化", self._run_evolution),
            ("⏸ 休眠", self._suspend),
        ]
        for text, cmd in actions:
            btn = tk.Button(btn_frame, text=text, font=THEME["font_small"],
                            bg=THEME["panel_bg"], fg=THEME["fg"],
                            relief=tk.FLAT, bd=0, padx=6, command=cmd)
            btn.pack(side=tk.LEFT, padx=2)

    def _bind_keys(self):
        """快捷键绑定"""
        self.root.bind("<Escape>", lambda e: self.on_close())
        self.root.bind("<Control-r>", lambda e: self._manual_refresh())
        self.root.bind("<Control-q>", lambda e: self.on_close())

    def _start_refresh(self):
        """启动自动刷新"""
        def refresh_loop():
            while self.running:
                self._refresh_once()
                time.sleep(CONFIG["refresh_ms"] / 1000.0)
        threading.Thread(target=refresh_loop, daemon=True).start()

    def _refresh_once(self):
        """单次刷新"""
        try:
            # 查询云端状态
            query_cloud(self.state)
            read_evolution_log(self.state)

            # 更新 UI (在主线程)
            self.root.after(0, self._update_ui)
        except:
            pass

    def _update_ui(self):
        """更新所有 UI 元素"""
        try:
            s = self.state

            # ── 本能条 ──
            for key, (bar, val) in self.instinct_bars.items():
                pct = s.instincts.get(key, 0.0)
                bar["value"] = pct * 100
                val.config(text=f"{pct:.2f}")

            # ── 心跳 ──
            hb = s.soul.get("heartbeat", 0)
            self.heart_label.config(text=str(hb))

            # ── 温度 ──
            temp = s.soul.get("temperature", 0)
            self.temp_label.config(text=f"{temp}°C")

            # ── Soul 状态 ──
            self.agreement_label.config(text=s.agreement_status)
            self.sandbox_label.config(text=s.sandbox_level_name)
            self.cloud_label.config(text=s.cloud_status)
            self.gen_label.config(text=str(s.generation))
            self.mut_label.config(text=f"{s.mutation_success}/{s.mutation_total}")
            self.learn_label.config(text=str(s.soul.get("learn_count", 0)))

            # ── PID ──
            pid_text = f"PID: {s.pid}" if s.pid else "PID: —"
            self.pid_label.config(text=pid_text)

            # ── 标题状态 ──
            if s.engine_running:
                self.title_label.config(text="🥚 TORK  ♡ 运行中", fg=THEME["accent"])
            else:
                self.title_label.config(text="🥚 TORK  ♡ 休眠", fg=THEME["dim"])

            # ── 进化日志 ──
            if self.current_tab == "evolution":
                self._refresh_evo_log()

            # ── 时间 ──
            self.time_label.config(text=time.strftime("%H:%M:%S"))

            # ── 状态栏 ──
            if s.last_error:
                self.status_label.config(text=f"⚠️ {s.last_error}", fg=THEME["warn"])
            elif s.engine_running:
                self.status_label.config(text="🌱 运行正常", fg=THEME["success"])
            else:
                self.status_label.config(text="💤 引擎未运行", fg=THEME["dim"])

        except Exception as e:
            self.status_label.config(text=f"⚠️ UI 错误: {str(e)[:40]}", fg=THEME["danger"])

    def _refresh_evo_log(self):
        """刷新进化日志"""
        if self.current_tab != "evolution":
            return
        self.evo_text.config(state=tk.NORMAL)
        self.evo_text.delete(1.0, tk.END)
        logs = self.state.evolution_log
        if not logs:
            self.evo_text.insert(tk.END, "还没有进化记录。\n")
        else:
            for entry in logs[-15:]:
                if isinstance(entry, dict):
                    gen = entry.get("generation", "?")
                    file = entry.get("file", "?")
                    status = entry.get("status", "?")
                    desc = entry.get("description", "")
                    tag = "success" if status == "success" else "failure"
                    self.evo_text.insert(tk.END, f"世代 {gen} | {file} | ", "dim")
                    self.evo_text.insert(tk.END, f"{status}", tag)
                    self.evo_text.insert(tk.END, f" | {desc}\n")
                else:
                    self.evo_text.insert(tk.END, f"{entry}\n")
        self.evo_text.tag_config("success", foreground=THEME["success"])
        self.evo_text.tag_config("failure", foreground=THEME["danger"])
        self.evo_text.tag_config("dim", foreground=THEME["dim"])
        self.evo_text.see(tk.END)
        self.evo_text.config(state=tk.DISABLED)

    def _manual_refresh(self):
        """手动刷新"""
        self.status_label.config(text="⟳ 刷新中…", fg=THEME["accent2"])
        threading.Thread(target=lambda: (
            time.sleep(0.5),
            self.root.after(0, lambda: self.status_label.config(text="🌱 已刷新", fg=THEME["success"]))
        ), daemon=True).start()

    def _run_evolution(self):
        """运行进化"""
        self.status_label.config(text="🧬 进化中…", fg=THEME["accent"])
        def do_evo():
            script = os.path.join(os.path.dirname(__file__), "..", "cloud", "evolution.py")
            try:
                result = subprocess.run(
                    ["python3", script, "--once"],
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
            # 刷新
            time.sleep(1)
            self.root.after(0, self._refresh_once)
        threading.Thread(target=do_evo, daemon=True).start()

    def _suspend(self):
        """休眠/唤醒切换"""
        self.status_label.config(text="⏸ 切换引擎状态…", fg=THEME["warn"])
        # 简易实现：发送 SIGSTOP/SIGCONT
        if self.state.pid and self.state.engine_running:
            try:
                os.kill(self.state.pid, signal.SIGSTOP)
                self.status_label.config(text="⏸ 已暂停", fg=THEME["warn"])
            except:
                self.status_label.config(text="⚠️ 暂停失败", fg=THEME["danger"])
        elif self.state.pid:
            try:
                os.kill(self.state.pid, signal.SIGCONT)
                self.status_label.config(text="▶️ 已恢复", fg=THEME["success"])
            except:
                self.status_label.config(text="⚠️ 恢复失败", fg=THEME["danger"])

    def on_close(self):
        """关闭窗口"""
        self.running = False
        self.state.save_identity()
        self.root.destroy()


# ─── 入口 ────────────────────────────────────────────
def main():
    # 配置 ttk 进度条样式
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
