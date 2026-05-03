#!/usr/bin/env python3
"""
TORK 悬浮窗 — 输入法风格 · 全局随叫随到
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
设计哲学：
  像输入法一样轻 — 按热键弹出，输完即走
  像心跳一样稳 — 后台常驻，永远在
  像徒弟一样灵 — 说着话就学会了
"""

import tkinter as tk
from tkinter import ttk
import subprocess, os, sys, time, threading, json, struct, signal

# ── 路径 ──
BASE = os.path.dirname(os.path.abspath(__file__))
API_DIR = os.path.join(BASE, 'api')
INBOX = os.path.join(BASE, 'inbox.md')
CONFIG_FILE = os.path.join(BASE, '.tork_floating.json')
sys.path.insert(0, API_DIR)

CONFIG_DEFAULTS = {
    'opacity': 0.92,
    'theme': 'dark',
    'hotkey': 'Control+Shift+T',
    'auto_hide_sec': 5,
    'font_size': 14,
    'height': 48,
    'width': 520,
    'api_key': ''
}

# ── 全局配置 ──
CFG = dict(CONFIG_DEFAULTS)

def load_config():
    global CFG
    try:
        if os.path.exists(CONFIG_FILE):
            with open(CONFIG_FILE) as f:
                CFG.update(json.load(f))
    except: pass

def save_config():
    try:
        with open(CONFIG_FILE, 'w') as f:
            json.dump(CFG, f, indent=2)
    except: pass

load_config()

# 从环境变量或配置读取 API Key
API_KEY = CFG.get('api_key') or os.environ.get('DEEPSEEK_API_KEY', '')


class TorkFloating:
    """TORK 悬浮窗 — 像输入法一样轻"""

    def __init__(self):
        self.root = tk.Tk()
        self.root.title("TORK")
        self.root.overrideredirect(True)          # 无边框
        self.root.attributes('-topmost', True)     # 永远在最前
        self.root.attributes('-alpha', 0.92)       # 半透明质感
        self.root.configure(bg='#1a1a1e')

        # 窗口尺寸
        self.W = 540
        self.H = 70
        self.root.geometry(f"{self.W}x{self.H}")

        # 状态
        self.visible = False
        self.api_connected = False
        self.tork_pid = self.find_tork()
        self.thinking = False

        # ── 构建 UI ──
        self.build_ui()

        # ── 后台线程 ──
        self._running = True
        self._last_cursor = (0, 0)
        threading.Thread(target=self._bg_loop, daemon=True).start()

        # ── 全局信号 ──
        self.root.bind('<Escape>', lambda e: self.hide())
        self.root.bind('<Return>', lambda e: self.send())

        # ── 初始化隐藏 ──
        self.root.withdraw()

    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    #  UI 构建
    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    def build_ui(self):
        """构建极简输入法风格界面"""
        main = tk.Frame(self.root, bg='#1a1a1e', highlightthickness=0)
        main.pack(fill=tk.BOTH, expand=True)

        # 顶部：TORK 状态条
        top = tk.Frame(main, bg='#1a1a1e', height=20)
        top.pack(fill=tk.X)
        top.pack_propagate(False)

        # 心跳指示
        self.heart_canvas = tk.Canvas(top, width=12, height=12, bg='#1a1a1e', highlightthickness=0)
        self.heart_canvas.pack(side=tk.LEFT, padx=(8, 4), pady=2)
        self.heart_dot = self.heart_canvas.create_oval(2, 2, 10, 10, fill='#555', outline='')

        # 状态文字
        self.lbl_status = tk.Label(top, text="TORK · 待命", bg='#1a1a1e', fg='#666',
                                   font=('Sans', 9), anchor=tk.W)
        self.lbl_status.pack(side=tk.LEFT, padx=2)

        # API 状态
        self.lbl_api = tk.Label(top, text="🌐 ○", bg='#1a1a1e', fg='#555',
                                font=('Sans', 9), anchor=tk.E)
        self.lbl_api.pack(side=tk.RIGHT, padx=8)

        # tick 计数
        self.lbl_tick = tk.Label(top, text="tick:--", bg='#1a1a1e', fg='#444',
                                 font=('Sans', 8), anchor=tk.E)
        self.lbl_tick.pack(side=tk.RIGHT, padx=4)

        # 中间：输入框（大，像输入法候选框）
        input_frame = tk.Frame(main, bg='#25252b')
        input_frame.pack(fill=tk.BOTH, expand=True, padx=6, pady=(2, 6))

        self.input_var = tk.StringVar()
        self.entry = tk.Entry(input_frame, textvariable=self.input_var,
                               bg='#25252b', fg='#e0e0e0',
                               insertbackground='#569cd6',
                               font=('Sans', CFG['font_size']),
                               bd=0, relief=tk.FLAT,
                               highlightthickness=1,
                               highlightbackground='#3a3a42',
                               highlightcolor='#569cd6')
        self.entry.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)
        self.entry.focus_set()

        # 底部：提示
        bottom = tk.Frame(main, bg='#1a1a1e', height=16)
        bottom.pack(fill=tk.X)
        self.lbl_hint = tk.Label(bottom, text="Ctrl+Shift+T 召唤 · Enter 执行 · Esc 关闭",
                                 bg='#1a1a1e', fg='#444', font=('Sans', 8))
        self.lbl_hint.pack(side=tk.LEFT, padx=8)

        # 点击事件 — 保持焦点
        self.root.bind('<Button-1>', lambda e: self.entry.focus_set())

    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    #  显示/隐藏
    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    def show(self):
        """在鼠标位置弹出（输入法风格）"""
        try:
            out = subprocess.run(['xdotool', 'getmouselocation'],
                                 capture_output=True, text=True, timeout=1)
            parts = out.stdout.strip().split()
            x = int(parts[0].split(':')[1]) if parts else 400
            y = int(parts[1].split(':')[1]) if len(parts) > 1 else 300
        except:
            x, y = 400, 300

        # 防止跑出屏幕右/下边界
        screen_w = self.root.winfo_screenwidth()
        screen_h = self.root.winfo_screenheight()
        x = min(x, screen_w - self.W - 20)
        y = min(y, screen_h - self.H - 20)
        x = max(x, 10)
        y = max(y, 10)

        self.root.geometry(f"{self.W}x{self.H}+{x}+{y}")
        self.input_var.set('')
        self.entry.focus_set()
        self.root.deiconify()
        self.root.lift()
        self.visible = True
        self.lbl_hint.config(text="Enter 执行 · Esc 关闭")
        self.root.after(100, self.entry.focus_set)

    def hide(self):
        """隐藏浮窗"""
        self.root.withdraw()
        self.visible = False

    def toggle(self):
        """切换显隐"""
        if self.visible:
            self.hide()
        else:
            self.show()

    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    #  核心：发送 & 执行
    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    def send(self, event=None):
        """发送输入到 DeepSeek API 并执行"""
        text = self.input_var.get().strip()
        if not text:
            self.hide()
            return

        self.thinking = True
        self.set_status("🧠 思考中...", '#dcdcaa')
        self.lbl_hint.config(text="正在处理...")
        self.root.update()

        # 后台处理
        threading.Thread(target=self._process, args=(text,), daemon=True).start()

    def _process(self, text):
        """处理用户输入"""
        try:
            if API_KEY and self.api_connected:
                reply = self._call_api(text)
                self.root.after(0, lambda: self._show_result(reply))
            else:
                # 离线模式：本地关键词匹配
                self._local_exec(text)
        except Exception as e:
            self.root.after(0, lambda: self._show_result(f"❌ 错误: {str(e)}"))

    def _call_api(self, text):
        """调用 DeepSeek API"""
        import requests
        payload = {
            "model": "deepseek-chat",
            "messages": [
                {"role": "system", "content": "你叫 TORK。用户在本地终端向你说话。"
                 "你的回复应简短（<200字）。如果是代码/命令请求，用 ``` 标注。"},
                {"role": "user", "content": text}
            ],
            "temperature": 0.7,
            "max_tokens": 1024
        }
        headers = {
            "Authorization": f"Bearer {API_KEY}",
            "Content-Type": "application/json"
        }
        resp = requests.post(
            "https://api.deepseek.com/v1/chat/completions",
            json=payload, headers=headers, timeout=30
        )
        if resp.status_code == 200:
            reply = resp.json()['choices'][0]['message']['content']
            # 检查是否有命令要执行
            self._extract_and_run(reply)
            return reply[:500]
        return f"🌐 API {resp.status_code}"

    def _local_exec(self, text):
        """本地关键词匹配执行"""
        text_lower = text.lower()
        result = ""

        if '心跳' in text or 'tork' in text_lower:
            pid = self.find_tork()
            if pid:
                result = f"❤ TORK 正在运行 (PID {pid})"
                self._read_tork_soul(pid)
            else:
                result = "💤 TORK 未运行"

        elif '编译' in text or 'build' in text_lower:
            subprocess.Popen(['make', '-C', BASE], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            result = "🔨 编译中..."

        elif '运行' in text or 'run' in text_lower:
            tork_bin = os.path.join(BASE, 'build', 'tork_core')
            if os.path.exists(tork_bin):
                subprocess.Popen([tork_bin])
                result = "▶ TORK 已启动"
            else:
                result = "❌ 未编译，请先编译"

        elif '状态' in text or 'status' in text_lower:
            pid = self.find_tork()
            result = f"TORK: {'❤ PID ' + str(pid) if pid else '💤 停止'}"
            result += f"\nAPI: {'✅ 已连接' if self.api_connected else '❌ 未连'}"
            result += f"\n浮窗: {'👁 显示' if self.visible else '🔇 隐藏'}"

        elif '帮助' in text or 'help' in text:
            result = """TORK 悬浮窗命令:
  心跳/tork  — 检查 TORK 进程
  编译/build — 编译 C/ASM 引擎
  运行/run   — 启动 TORK Core
  状态       — 系统状态
  帮助       — 显示此帮助
  清屏/clear — 清空输入
  重启       — 重启 TORK
  其他输入将尝试通过 DeepSeek API 回答"""

        elif '重启' in text:
            pid = self.find_tork()
            if pid:
                os.kill(pid, signal.SIGTERM)
                time.sleep(0.5)
            tork_bin = os.path.join(BASE, 'build', 'tork_core')
            if os.path.exists(tork_bin):
                subprocess.Popen([tork_bin])
                result = "🔄 TORK 已重启"
            else:
                result = "❌ 未找到 TORK Core"

        elif '清屏' in text or 'clear' in text_lower:
            result = ""

        else:
            result = f"🤖 TORK: 收到「{text}」\n💡 连接 API 后我可以更聪明。输入「帮助」查看命令。"

        self.root.after(0, lambda: self._show_result(result))

    def _extract_and_run(self, reply):
        """从 API 回复中提取代码块并执行"""
        import re
        # Shell 命令
        cmds = re.findall(r'```(?:bash|sh)\n(.*?)```', reply, re.DOTALL)
        for cmd in cmds:
            cmd = cmd.strip()
            if cmd and not cmd.startswith('#'):
                subprocess.Popen(cmd, shell=True)

        # 写入收件箱
        code_blocks = re.findall(r'```(\w+)\n(.*?)```', reply, re.DOTALL)
        if code_blocks:
            with open(INBOX, 'a') as f:
                f.write(f"\n<!-- from api {time.strftime('%H:%M:%S')} -->\n")
                for lang, code in code_blocks:
                    f.write(f"```{lang}\n{code}\n```\n\n")

    def _show_result(self, text):
        """显示结果（临时放大窗口显示回复）"""
        if not text:
            self.hide()
            return

        # 用通知窗口展示结果
        self.lbl_hint.config(text=text[:60] + ('...' if len(text) > 60 else ''))
        self.set_status("✅ 完成", '#6a9955')
        self.thinking = False

        # 自动隐藏（5秒后）
        self.root.after(CFG['auto_hide_sec'] * 1000, self.hide)

    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    #  TORK 进程管理
    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    def find_tork(self):
        try:
            r = subprocess.run(['pgrep', '-x', 'tork_core'], capture_output=True, text=True, timeout=2)
            if r.stdout.strip():
                return int(r.stdout.strip().split('\n')[0])
        except: pass
        return None

    def _read_tork_soul(self, pid):
        try:
            with open(f'/proc/{pid}/mem', 'rb') as f:
                f.seek(0x200000)
                data = f.read(96)
                if len(data) >= 40:
                    tick = struct.unpack_from('<I', data, 0)[0]
                    hw_stress = data[0x24]
                    mode = data[0x25]
                    self.lbl_tick.config(text=f"tick:{tick}")
                    return tick, hw_stress, mode
        except: pass
        return None

    def set_status(self, text, color='#666'):
        self.lbl_status.config(text=text, fg=color)
        if '❤' in text or '运行' in text:
            self.heart_canvas.itemconfig(self.heart_dot, fill='#6a9955')
        elif '思考' in text:
            self.heart_canvas.itemconfig(self.heart_dot, fill='#dcdcaa')
        elif '待命' in text or '未运行' in text:
            self.heart_canvas.itemconfig(self.heart_dot, fill='#555')
        elif '完成' in text:
            self.heart_canvas.itemconfig(self.heart_dot, fill='#569cd6')

    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    #  后台循环
    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    def _bg_loop(self):
        """后台：监控 TORK 进程 + 检查 API 连接 + 心跳"""
        last_tork_check = 0
        while self._running:
            try:
                now = time.time()
                # 每秒检查 TORK 进程
                pid = self.find_tork()
                if pid != self.tork_pid:
                    self.tork_pid = pid
                    if pid:
                        data = self._read_tork_soul(pid)
                        self.root.after(0, lambda: self.set_status(f"❤ TORK (PID {pid})", '#6a9955'))
                    else:
                        self.root.after(0, lambda: self.set_status("💤 TORK 未运行", '#555'))

                # 每 5 秒检查 API
                if now - last_tork_check > 5:
                    last_tork_check = now
                    if API_KEY and not self.api_connected:
                        try:
                            import requests
                            r = requests.get("https://api.deepseek.com/v1/models",
                                             headers={"Authorization": f"Bearer {API_KEY}"},
                                             timeout=5)
                            if r.status_code == 200:
                                self.api_connected = True
                                self.root.after(0, lambda: self.lbl_api.config(text="🌐 ● 在线", fg='#6a9955'))
                            else:
                                self.root.after(0, lambda: self.lbl_api.config(text="🌐 ○ 离线", fg='#555'))
                        except:
                            self.root.after(0, lambda: self.lbl_api.config(text="🌐 ○ 离线", fg='#555'))

                time.sleep(1)
            except:
                time.sleep(1)

    def stop(self):
        self._running = False
        self.root.quit()

    def run(self):
        """启动主循环"""
        # 初始状态
        if self.tork_pid:
            self.set_status(f"❤ TORK (PID {self.tork_pid})", '#6a9955')
        if API_KEY:
            self.lbl_api.config(text="🌐 ○ 检测中...", fg='#888')
        self.root.protocol("WM_DELETE_WINDOW", self.stop)
        self.root.mainloop()


# ── 信号文件 IPC（被热键守护进程唤出） ──
# 守护进程写 /tmp/tork.flag，浮窗轮询读取
SIGNAL_FILE = '/tmp/tork.flag'

def _check_signal(app):
    """轮询信号文件"""
    while app._running:
        try:
            if os.path.exists(SIGNAL_FILE):
                with open(SIGNAL_FILE) as f:
                    cmd = f.read().strip()
                os.unlink(SIGNAL_FILE)
                if cmd == 'toggle':
                    app.root.after(0, app.toggle)
                elif cmd == 'show':
                    app.root.after(0, app.show)
                elif cmd == 'hide':
                    app.root.after(0, app.hide)
                elif cmd == 'quit':
                    app.root.after(0, app.stop)
        except: pass
        time.sleep(0.2)




def main():
    app = TorkFloating()
    threading.Thread(target=_check_signal, args=(app,), daemon=True).start()
    if len(sys.argv) > 1 and sys.argv[1] == '--show':
        app.show()
    app.run()

if __name__ == '__main__':
    main()
