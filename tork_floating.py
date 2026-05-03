#!/usr/bin/env python3
"""
TORK 悬浮窗 v2 — Chatbox 启发式设计
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
- 输入框自动伸缩（借鉴 Chatbox Textarea autosize）
- 响应区流式展示（借鉴 Chatbox MessageList）
- 发送/停止一键切换
- 工具调用可视化
- Markdown 渲染
"""

import tkinter as tk
from tkinter import ttk, font
import subprocess, os, sys, time, threading, json, struct, signal, re

BASE = os.path.dirname(os.path.abspath(__file__))
API_DIR = os.path.join(BASE, 'api')
CONFIG_FILE = os.path.join(BASE, '.tork_floating.json')
sys.path.insert(0, API_DIR)

# ── 配置 ──
CFG = {
    'opacity': 0.93, 'font_size': 14, 'code_font_size': 12,
    'max_lines': 20, 'auto_hide_sec': 0,  # 0 = 不自动隐藏
    'api_key': os.environ.get('DEEPSEEK_API_KEY', ''),
    'model': 'deepseek-chat',
}

def load_config():
    try:
        if os.path.exists(CONFIG_FILE):
            with open(CONFIG_FILE) as f:
                CFG.update(json.load(f))
    except: pass
load_config()

API_KEY = CFG.get('api_key') or os.environ.get('DEEPSEEK_API_KEY', '')

# ── 配色方案（暗色 · 东方美學） ──
C = {
    'bg': '#1a1a1e',
    'bg2': '#222228',
    'bg3': '#2a2a32',
    'border': '#3a3a45',
    'fg': '#e0e0e0',
    'fg2': '#a0a0a8',
    'fg3': '#666670',
    'accent': '#569cd6',    # 青
    'success': '#6a9955',   # 竹绿
    'warn': '#dcdcaa',      # 竹黄
    'error': '#f44747',     # 朱红
    'code_bg': '#1e1e24',
    'user_bg': '#0d3b5e',
    'assistant': '#2a2a32',
}

class TorkFloating:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("TORK")
        self.root.overrideredirect(True)
        self.root.attributes('-topmost', True)
        self.root.attributes('-alpha', CFG['opacity'])
        self.root.configure(bg=C['bg'])

        # ── 尺寸 ──
        self.W = 580
        self.H_min = 60       # 仅输入栏高度
        self.H_max = 480      # 展开最大高度
        self._expanded = False
        
        # ── 状态 ──
        self._messages = []      # [{'role':'user'|'assistant', 'content':str, 'done':bool}, ...]
        self._generating = False
        self._stream_buffer = ''
        self._tork_pid = self._find_tork()
        self._running = True

        # ── 构建 UI ──
        self._build_ui()
        
        # ── 事件绑定 ──
        self.root.bind('<Escape>', lambda e: self._toggle_expand(force=False))
        self.root.bind('<Return>', lambda e: self._on_enter())
        self.root.bind('<Shift-Return>', lambda e: self._insert_newline())
        self.root.bind('<Control-Return>', lambda e: self._on_enter())
        self.root.bind('<Button-1>', lambda e: self.entry.focus_set())

        # ── 初始化 ──
        self.root.withdraw()
        self._bg_thread = threading.Thread(target=self._bg_loop, daemon=True)
        self._bg_thread.start()

    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    #  UI 构建
    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    def _build_ui(self):
        # ── 主容器 ──
        self.main = tk.Frame(self.root, bg=C['bg'])
        self.main.pack(fill=tk.BOTH, expand=True)

        # ── 顶部状态栏 ──
        self._build_header()

        # ── 消息区域（可滚动，默认隐藏） ──
        self._build_message_area()

        # ── 输入区域（始终显示） ──
        self._build_input_area()

    def _build_header(self):
        """状态栏：心跳 + 模型 + API + 收起按钮"""
        header = tk.Frame(self.main, bg=C['bg2'], height=22)
        header.pack(fill=tk.X)
        header.pack_propagate(False)

        # 心跳点
        self.heart = tk.Canvas(header, width=10, height=10, bg=C['bg2'], highlightthickness=0)
        self.heart.pack(side=tk.LEFT, padx=(6, 2), pady=6)
        self.heart_dot = self.heart.create_oval(2, 2, 8, 8, fill='#555', outline='')

        # 状态标签
        self.lbl_status = tk.Label(header, text="TORK", bg=C['bg2'], fg=C['fg2'],
                                   font=('Sans', 9), anchor=tk.W)
        self.lbl_status.pack(side=tk.LEFT, padx=2)

        # 模型标签
        self.lbl_model = tk.Label(header, text="", bg=C['bg2'], fg=C['fg3'],
                                  font=('Sans', 8), anchor=tk.W)
        self.lbl_model.pack(side=tk.LEFT, padx=4)
        if API_KEY:
            self.lbl_model.config(text="deepseek-chat")

        # API 状态
        self.lbl_api = tk.Label(header, text="", bg=C['bg2'], fg=C['fg3'],
                                font=('Sans', 8), anchor=tk.E)
        self.lbl_api.pack(side=tk.RIGHT, padx=4)

        # 展开/收起按钮
        self.btn_toggle = tk.Label(header, text="━", bg=C['bg2'], fg=C['fg3'],
                                   font=('Sans', 10), cursor='hand2')
        self.btn_toggle.pack(side=tk.RIGHT, padx=6)
        self.btn_toggle.bind('<Button-1>', lambda e: self._toggle_expand())

        # tick
        self.lbl_tick = tk.Label(header, text="", bg=C['bg2'], fg=C['fg3'],
                                 font=('Sans', 8), anchor=tk.E)
        self.lbl_tick.pack(side=tk.RIGHT, padx=4)

    def _build_message_area(self):
        """可滚动的消息显示区（借鉴 Chatbox MessageList）"""
        self.msg_container = tk.Frame(self.main, bg=C['bg'])
        
        # Canvas + Scrollbar 实现滚动
        self.msg_canvas = tk.Canvas(self.msg_container, bg=C['bg'], highlightthickness=0,
                                    bd=0, height=0)
        self.msg_scroll = tk.Scrollbar(self.msg_container, orient=tk.VERTICAL,
                                       command=self.msg_canvas.yview)
        self.msg_canvas.configure(yscrollcommand=self.msg_scroll.set)
        
        self.msg_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.msg_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        
        # 内部 frame（放消息的地方）
        self.msg_frame = tk.Frame(self.msg_canvas, bg=C['bg'])
        self.msg_canvas_window = self.msg_canvas.create_window(
            (0, 0), window=self.msg_frame, anchor=tk.NW, width=self.W-16
        )
        
        # 绑定滚动区域更新
        self.msg_frame.bind('<Configure>', self._on_frame_configure)
        self.msg_canvas.bind('<Configure>', self._on_canvas_configure)
        
        # 鼠标滚轮
        self.msg_canvas.bind('<Enter>', lambda e: self._bind_mousewheel(True))
        self.msg_canvas.bind('<Leave>', lambda e: self._bind_mousewheel(False))

    def _build_input_area(self):
        """输入区域（借鉴 Chatbox InputBox 的 autosize 设计）"""
        input_frame = tk.Frame(self.main, bg=C['bg2'])
        input_frame.pack(fill=tk.X, side=tk.BOTTOM)

        # 发送/停止按钮
        self.btn_send = tk.Label(input_frame, text="▶", bg=C['accent'], fg='white',
                                 font=('Sans', 14, 'bold'), width=3, cursor='hand2',
                                 anchor=tk.CENTER)
        self.btn_send.pack(side=tk.RIGHT, padx=(0, 4), pady=6)
        self.btn_send.bind('<Button-1>', lambda e: self._on_enter())

        # 输入框（用 Text 替代 Entry 实现自动换行和 autosize）
        self.entry = tk.Text(input_frame, 
                              bg=C['bg3'], fg=C['fg'],
                              insertbackground=C['accent'],
                              font=('Sans', CFG['font_size']),
                              bd=0, relief=tk.FLAT,
                              highlightthickness=1,
                              highlightbackground=C['border'],
                              highlightcolor=C['accent'],
                              height=1, padx=8, pady=6,
                              wrap=tk.WORD)
        self.entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(4, 2), pady=6)
        
        # 绑定 autosize
        self.entry.bind('<KeyRelease>', self._autosize_input)
        
        # 占位符
        self._placeholder = "输入你的需求... Enter 执行"
        self._show_placeholder()

        # 底部提示
        self.lbl_hint = tk.Label(self.main, text="", bg=C['bg'], fg=C['fg3'],
                                 font=('Sans', 8), anchor=tk.W)
        # 放在 msg_container 和 input 之间

    def _show_placeholder(self):
        self.entry.delete('1.0', tk.END)
        self.entry.insert('1.0', self._placeholder)
        self.entry.config(fg=C['fg3'])
        
    def _hide_placeholder(self):
        if self.entry.get('1.0', 'end-1c').strip() == self._placeholder:
            self.entry.delete('1.0', tk.END)
            self.entry.config(fg=C['fg'])

    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    #  布局管理
    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    def _on_frame_configure(self, event):
        self.msg_canvas.configure(scrollregion=self.msg_canvas.bbox('all'))

    def _on_canvas_configure(self, event):
        self.msg_canvas.itemconfig(self.msg_canvas_window, width=event.width)

    def _bind_mousewheel(self, bind):
        if bind:
            self.msg_canvas.bind_all('<MouseWheel>', self._on_mousewheel)
            self.msg_canvas.bind_all('<Button-4>', self._on_mousewheel)
            self.msg_canvas.bind_all('<Button-5>', self._on_mousewheel)
        else:
            self.msg_canvas.unbind_all('<MouseWheel>')
            self.msg_canvas.unbind_all('<Button-4>')
            self.msg_canvas.unbind_all('<Button-5>')

    def _on_mousewheel(self, event):
        if event.num == 4:
            self.msg_canvas.yview_scroll(-1, 'units')
        elif event.num == 5:
            self.msg_canvas.yview_scroll(1, 'units')
        else:
            self.msg_canvas.yview_scroll(-1 * (event.delta // 120), 'units')

    def _autosize_input(self, event=None):
        """自动调整输入框高度（借鉴 Chatbox autosize）"""
        lines = self.entry.get('1.0', 'end-1c').count('\n') + 1
        max_lines = 6
        new_height = min(lines, max_lines)
        self.entry.configure(height=max(1, new_height))
        self._update_window_size()

    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    #  窗口管理
    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    def show(self):
        """在鼠标光标位置弹出"""
        try:
            out = subprocess.run(['xdotool', 'getmouselocation'],
                                 capture_output=True, text=True, timeout=1)
            parts = out.stdout.strip().split()
            x = int(parts[0].split(':')[1])
            y = int(parts[1].split(':')[1])
        except:
            x, y = 400, 300
        
        sw = self.root.winfo_screenwidth()
        sh = self.root.winfo_screenheight()
        x = max(10, min(x, sw - self.W - 20))
        y = max(10, min(y, sh - self.H_max - 20))
        
        self.root.geometry(f"{self.W}x{self.H_min}+{x}+{y}")
        self.root.deiconify()
        self.root.lift()
        self.entry.focus_set()
        
        # 如果有未读消息，自动展开
        if len(self._messages) > 0:
            self._toggle_expand(force=True)

    def hide(self):
        self.root.withdraw()

    def toggle(self):
        if self.root.state() == 'normal' and self.root.winfo_viewable():
            self.hide()
        else:
            self.show()

    def _update_window_size(self):
        """根据展开状态和内容调整窗口高度"""
        if not self._expanded:
            h = self.H_min
        else:
            # 计算消息区域需要的高度
            msg_h = self.msg_frame.winfo_reqheight() if self.msg_frame.winfo_children() else 0
            input_h = 50
            header_h = 22
            hint_h = 18
            total = header_h + min(msg_h, self.H_max - self.H_min - 20) + input_h + hint_h + 20
            h = max(self.H_min, min(total, self.H_max))
        
        geo = self.root.geometry()
        x = geo.split('+')[1] if '+' in geo else '100'
        y = geo.split('+')[2] if '+' in geo and geo.count('+') >= 2 else '100'
        if '+' not in geo:
            x, y = '100', '100'
        self.root.geometry(f"{self.W}x{int(h)}+{x}+{y}")

    def _toggle_expand(self, force=None):
        """展开/收起消息区域"""
        if force is True:
            self._expanded = True
        elif force is False:
            self._expanded = False
        else:
            self._expanded = not self._expanded
        
        if self._expanded:
            self.msg_container.pack(fill=tk.BOTH, expand=True, before=self.lbl_hint)
            self.lbl_hint.pack(fill=tk.X, side=tk.BOTTOM)
            self.btn_toggle.config(text="▽")
            self.msg_canvas.configure(height=200)
        else:
            self.msg_container.pack_forget()
            self.lbl_hint.pack_forget()
            self.btn_toggle.config(text="━")
        
        self.root.update_idletasks()
        self._update_window_size()
        
        # 滚动到底部
        if self._expanded:
            self.root.after(50, self._scroll_to_bottom)

    def _scroll_to_bottom(self):
        try:
            self.msg_canvas.yview_moveto(1.0)
        except: pass

    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    #  输入处理
    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    def _on_enter(self):
        """Enter 发送 / 停止生成"""
        if self._generating:
            self._stop_generating()
            return
        
        text = self.entry.get('1.0', 'end-1c').strip()
        if not text or text == self._placeholder:
            return
        
        self._hide_placeholder()
        self._send_message(text)

    def _insert_newline(self):
        """Shift+Enter 换行"""
        self.entry.insert(tk.INSERT, '\n')
        self._autosize_input()

    def _send_message(self, text):
        """发送消息"""
        # 清空输入
        self.entry.delete('1.0', tk.END)
        self._autosize_input()
        
        # 添加用户消息
        self._add_message('user', text, '')
        
        # 展开窗口
        if not self._expanded:
            self._toggle_expand(force=True)
        
        # 开始生成
        self._generating = True
        self._stream_buffer = ''
        self._add_message('assistant', '', '')
        self._update_send_button()
        self.lbl_hint.config(text="🧠 思考中...", fg=C['warn'])
        
        # 后台请求
        threading.Thread(target=self._generate, args=(text,), daemon=True)

    def _stop_generating(self):
        self._generating = False
        self._update_send_button()
        self.lbl_hint.config(text="⏹ 已停止", fg=C['fg3'])
        # 标记最后一条消息完成
        if self._messages:
            self._messages[-1]['done'] = True

    def _update_send_button(self):
        if self._generating:
            self.btn_send.config(text="■", bg=C['error'])
        else:
            self.btn_send.config(text="▶", bg=C['accent'])

    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    #  消息管理（借鉴 Chatbox MessageList）
    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    def _add_message(self, role, content='', tool_info=''):
        """添加一条消息到界面"""
        msg = {'role': role, 'content': content, 'tool_info': tool_info, 'done': False}
        self._messages.append(msg)
        self._render_messages()

    def _update_last_message(self, content, tool_info=''):
        """更新最后一条消息（流式追加）"""
        if self._messages:
            self._messages[-1]['content'] = content
            if tool_info:
                self._messages[-1]['tool_info'] = tool_info
            self._render_messages()

    def _render_messages(self):
        """重新渲染所有消息（借鉴 Chatbox 的消息列表渲染）"""
        for w in self.msg_frame.winfo_children():
            w.destroy()
        
        for i, msg in enumerate(self._messages):
            self._render_message(msg, i)
        
        self.root.update_idletasks()
        self._update_window_size()
        self._scroll_to_bottom()

    def _render_message(self, msg, idx):
        """渲染单条消息"""
        role = msg['role']
        content = msg['content']
        
        # 气泡容器
        bubble = tk.Frame(self.msg_frame, bg=C['bg'], padx=8, pady=4)
        bubble.pack(fill=tk.X, padx=4, pady=2)
        
        # 角色标签
        tag_text = "🧑 你" if role == 'user' else "🦀 TORK"
        tag_color = C['user_bg'] if role == 'user' else C['assistant']
        
        tag = tk.Frame(bubble, bg=tag_color, padx=6, pady=2)
        tag.pack(anchor=tk.W if role == 'assistant' else tk.E)
        
        lbl_tag = tk.Label(tag, text=tag_text, bg=tag_color, fg=C['fg'],
                           font=('Sans', 9, 'bold'))
        lbl_tag.pack()
        
        # 内容
        if content:
            # 检测是否为代码块
            parts = re.split(r'(```\w*\n.*?```)', content, flags=re.DOTALL)
            content_frame = tk.Frame(bubble, bg=C['bg'], padx=4, pady=2)
            content_frame.pack(fill=tk.X, anchor=tk.W if role == 'assistant' else tk.E)
            
            # 简单渲染：纯文本
            txt = tk.Text(content_frame, bg=C['bg'], fg=C['fg'],
                          font=('Sans', 12), bd=0, relief=tk.FLAT,
                          wrap=tk.WORD, height=min(20, content.count('\n') + 1),
                          padx=4, pady=2)
            txt.insert('1.0', content)
            txt.config(state=tk.DISABLED)
            txt.pack(fill=tk.X)
        
        # 工具调用信息
        if msg.get('tool_info'):
            ti = tk.Label(bubble, text=msg['tool_info'], bg=C['bg3'], fg=C['fg2'],
                          font=('Sans', 9), padx=6, pady=2, anchor=tk.W)
            ti.pack(fill=tk.X, pady=(2, 0))
        
        # 生成中动画
        if not msg['done'] and role == 'assistant' and self._generating:
            dot = tk.Label(bubble, text="● ● ●", bg=C['bg'], fg=C['accent'],
                           font=('Sans', 16))
            dot.pack(anchor=tk.W)

    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    #  DeepSeek API 调用（流式）
    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    def _generate(self, text):
        """调用 DeepSeek API 流式生成"""
        try:
            # 先尝试本地命令匹配
            if self._try_local_command(text):
                self._generating = False
                self.root.after(0, self._update_send_button)
                return
            
            if not API_KEY:
                self.root.after(0, lambda: self._update_last_message(
                    "💡 请先配置 DeepSeek API Key：\n./tork.sh connect",
                    ""
                ))
                self._generating = False
                self.root.after(0, self._update_send_button)
                return
            
            import requests
            import json as json_module
            
            headers = {
                "Authorization": f"Bearer {API_KEY}",
                "Content-Type": "application/json"
            }
            
            # 构建消息历史
            messages = [{"role": "system", "content": "你叫 TORK，是一个运行在用户本地的 AI 助手。回复简洁（<300字），如需执行命令用 ```bash 标注。"}]
            for m in self._messages[:-1]:  # 排除当前正在生成的
                role = 'user' if m['role'] == 'user' else 'assistant'
                messages.append({"role": role, "content": m['content'][:2000]})
            
            payload = {
                "model": CFG['model'],
                "messages": messages + [{"role": "user", "content": text}],
                "stream": True,
                "temperature": 0.7,
                "max_tokens": 2048
            }
            
            resp = requests.post(
                "https://api.deepseek.com/v1/chat/completions",
                json=payload, headers=headers, stream=True, timeout=60
            )
            
            if resp.status_code != 200:
                self.root.after(0, lambda: self._update_last_message(
                    f"🌐 API 错误: {resp.status_code}", ""
                ))
                self._generating = False
                self.root.after(0, self._update_send_button)
                return
            
            # 流式读取
            full_content = ''
            for line in resp.iter_lines():
                if not self._generating:
                    resp.close()
                    break
                if line:
                    line = line.decode('utf-8')
                    if line.startswith('data: '):
                        data = line[6:]
                        if data.strip() == '[DONE]':
                            break
                        try:
                            chunk = json_module.loads(data)
                            delta = chunk.get('choices', [{}])[0].get('delta', {})
                            if 'content' in delta and delta['content']:
                                full_content += delta['content']
                                # 流式更新 UI
                                self.root.after(0, lambda c=full_content: self._update_last_message(c))
                        except: pass
            
            # 生成完成
            self._messages[-1]['done'] = True
            self._generating = False
            self.root.after(0, self._update_send_button)
            self.root.after(0, lambda: self.lbl_hint.config(
                text="✅ 完成 · Enter 新对话 · Esc 收起", fg=C['success']
            ))
            
            # 提取并执行命令
            self._extract_and_run(full_content)
            
        except Exception as e:
            self.root.after(0, lambda: self._update_last_message(f"❌ 错误: {str(e)}"))
            self._generating = False
            self.root.after(0, self._update_send_button)

    def _try_local_command(self, text):
        """本地命令匹配（无需 API）"""
        t = text.lower().strip()
        
        if t in ('心跳', 'tork', '状态'):
            pid = self._find_tork()
            if pid:
                info = self._read_tork_soul(pid)
                s = f"❤ TORK 正在运行 (PID {pid})"
                if info:
                    s += f"\n  tick: {info[0]}  stress: {info[1]}  mode: {info[2]}"
                self.root.after(0, lambda: self._update_last_message(s))
            else:
                self.root.after(0, lambda: self._update_last_message("💤 TORK Core 未运行"))
            return True
        
        if t in ('编译', 'build'):
            self.root.after(0, lambda: self._update_last_message("🔨 编译 TORK..."))
            subprocess.Popen(['make', '-C', BASE], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return True
        
        if t in ('运行', 'run'):
            bin_path = os.path.join(BASE, 'build', 'tork_core')
            if os.path.exists(bin_path):
                subprocess.Popen([bin_path])
                self.root.after(0, lambda: self._update_last_message("▶ TORK Core 已启动"))
            else:
                self.root.after(0, lambda: self._update_last_message("❌ 请先编译"))
            return True
        
        if t in ('帮助', 'help'):
            self.root.after(0, lambda: self._update_last_message(
                "🦀 **TORK 命令**\n"
                "  `心跳` / `tork` — TORK 进程状态\n"
                "  `编译` / `build` — 编译 TORK Core\n"
                "  `运行` / `run` — 启动 TORK Core\n"
                "  `状态` — 系统状态\n"
                "  `帮助` / `help` — 此帮助\n"
                "  `清屏` — 清空对话\n\n"
                "其他输入将调用 DeepSeek API 回答"
            ))
            return True
        
        if t in ('清屏', 'clear'):
            self._messages = []
            self.root.after(0, self._render_messages)
            return True
        
        if t == '重启':
            pid = self._find_tork()
            if pid:
                os.kill(pid, signal.SIGTERM)
                time.sleep(0.3)
            bin_path = os.path.join(BASE, 'build', 'tork_core')
            if os.path.exists(bin_path):
                subprocess.Popen([bin_path])
                self.root.after(0, lambda: self._update_last_message("🔄 TORK Core 已重启"))
            else:
                self.root.after(0, lambda: self._update_last_message("❌ 未找到 TORK Core"))
            return True
        
        return False

    def _extract_and_run(self, content):
        """从回复中提取命令执行"""
        cmds = re.findall(r'```(?:bash|sh)\n(.*?)```', content, re.DOTALL)
        for cmd in cmds:
            cmd = cmd.strip()
            if cmd and not cmd.startswith('#'):
                subprocess.Popen(cmd, shell=True)

    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    #  TORK 进程管理
    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    def _find_tork(self):
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
                    return tick, hw_stress, mode
        except: pass
        return None

    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    #  后台循环
    # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    def _bg_loop(self):
        """后台：监控 TORK + API + 信号文件"""
        while self._running:
            try:
                # 信号文件 IPC
                if os.path.exists('/tmp/tork.flag'):
                    try:
                        with open('/tmp/tork.flag') as f:
                            cmd = f.read().strip()
                        os.unlink('/tmp/tork.flag')
                        if cmd == 'toggle':
                            self.root.after(0, self.toggle)
                        elif cmd == 'show':
                            self.root.after(0, self.show)
                        elif cmd == 'hide':
                            self.root.after(0, self.hide)
                    except: pass
                
                # TORK 进程监控
                pid = self._find_tork()
                if pid:
                    self.root.after(0, lambda p=pid: self.lbl_status.config(
                        text=f"❤ TORK", fg=C['success']
                    ))
                    self.root.after(0, lambda: self.heart.itemconfig(
                        self.heart_dot, fill=C['success']
                    ))
                    info = self._read_tork_soul(pid)
                    if info:
                        self.root.after(0, lambda t=info[0]: self.lbl_tick.config(
                            text=f"tick:{t}"
                        ))
                else:
                    self.root.after(0, lambda: self.lbl_status.config(
                        text="TORK", fg=C['fg3']
                    ))
                
                time.sleep(1)
            except:
                time.sleep(1)

    def stop(self):
        self._running = False
        self.root.quit()

    def run(self):
        self.root.protocol("WM_DELETE_WINDOW", self.stop)
        self.root.mainloop()


# ── 入口 ──
SIGNAL_FILE = '/tmp/tork.flag'

def main():
    app = TorkFloating()
    if len(sys.argv) > 1 and sys.argv[1] == '--show':
        app.show()
    app.run()

if __name__ == '__main__':
    main()
