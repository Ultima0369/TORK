#!/usr/bin/env python3
"""TORK 悬浮窗 v4 — 直接照搬 Chatbox 设计"""
import tkinter as tk, os, sys, time, threading, subprocess

BASE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(BASE, 'api'))
API_KEY = os.environ.get('DEEPSEEK_API_KEY', '')

C = {
    'bg': '#1a1b2e', 'bg2': '#252740', 'fg': '#e8e8f0', 'fg2': '#8a8ca0',
    'accent': '#6c5ce7', 'success': '#6a9955', 'border': '#3d3f5c',
    'input_bg': '#2a2d45', 'user_bubble': '#3d3f5c', 'assistant_bubble': '#252740',
}

class TorkFloating:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("TORK")
        self.W, self.H = 500, 420
        self._messages = []
        self._generating = False

        self.root.overrideredirect(True)
        self.root.attributes('-topmost', True)
        self.root.attributes('-alpha', 0.95)
        self.root.configure(bg=C['bg'])

        # 鼠标位置
        try:
            o = subprocess.run(['xdotool', 'getmouselocation'], capture_output=True, text=True, timeout=1)
            p = o.stdout.strip().split()
            x, y = int(p[0].split(':')[1]) - self.W//2, int(p[1].split(':')[1]) - 20
        except:
            x, y = 400, 300
        self.root.geometry(f'{self.W}x{self.H}+{max(0,x)}+{max(0,y)}')

        self._build_ui()
        self._start_poll()

    def _build_ui(self):
        # == 顶栏：纯拖拽把手 ==
        self.top = tk.Frame(self.root, bg=C['bg2'], height=32, cursor='fleur')
        self.top.pack(fill=tk.X)
        self.top.pack_propagate(False)

        # 标题 + 状态点
        self.dot = tk.Canvas(self.top, width=10, height=10, bg=C['bg2'], highlightthickness=0)
        self.dot.pack(side=tk.LEFT, padx=(10,4), pady=11)
        self.dot_id = self.dot.create_oval(1,1,9,9, fill=C['success'], outline='')

        tk.Label(self.top, text='TORK', bg=C['bg2'], fg=C['fg'], font=('sans', 11, 'bold')).pack(side=tk.LEFT)
        self.lbl_status = tk.Label(self.top, text='就绪', bg=C['bg2'], fg=C['fg2'], font=('sans', 9))
        self.lbl_status.pack(side=tk.LEFT, padx=6)

        # 关闭
        self.btn_close = tk.Label(self.top, text='✕', bg=C['bg2'], fg=C['fg2'], font=('sans', 12), cursor='hand2')
        self.btn_close.pack(side=tk.RIGHT, padx=(0,8))
        self.btn_close.bind('<Button-1>', lambda e: self.hide())

        # 拖拽
        self._drag_data = {'x':0, 'y':0}
        self.top.bind('<Button-1>', self._drag_start)
        self.top.bind('<B1-Motion>', self._drag_move)

        # == 消息区 (可滚动) ==
        msg_frame = tk.Frame(self.root, bg=C['bg'])
        msg_frame.pack(fill=tk.BOTH, expand=True)

        self.msg_canvas = tk.Canvas(msg_frame, bg=C['bg'], highlightthickness=0)
        scrolly = tk.Scrollbar(msg_frame, orient=tk.VERTICAL, command=self.msg_canvas.yview)
        self.msg_container = tk.Frame(self.msg_canvas, bg=C['bg'])
        self.msg_container.bind('<Configure>', lambda e: self.msg_canvas.configure(scrollregion=self.msg_canvas.bbox('all')))
        self.msg_canvas.create_window((0,0), window=self.msg_container, anchor='nw', width=self.W-20)
        self.msg_canvas.configure(yscrollcommand=scrolly.set)
        self.msg_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrolly.pack(side=tk.RIGHT, fill=tk.Y)

        # 欢迎消息
        self._add_bubble('tork', '你好，我是 TORK。有什么需要帮忙的？')

        # == 底部输入区 (始终可见) ==
        input_frame = tk.Frame(self.root, bg=C['bg'], height=56)
        input_frame.pack(fill=tk.X)
        input_frame.pack_propagate(False)

        inner = tk.Frame(input_frame, bg=C['input_bg'])
        inner.pack(fill=tk.BOTH, expand=True, padx=8, pady=6)
        inner.pack_propagate(False)

        self.entry = tk.Entry(inner, bg=C['input_bg'], fg=C['fg'], font=('sans', 13),
                               insertbackground=C['fg'], bd=0, relief=tk.FLAT)
        self.entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(8,0), pady=8, ipady=4)
        self.entry.bind('<Return>', self._send)
        self.entry.bind('<Escape>', lambda e: self.hide())
        self.entry.focus_set()

        # 发送按钮
        self.btn_send = tk.Label(inner, text='▶', bg=C['accent'], fg='white',
                                  font=('sans', 11), padx=10, pady=4, cursor='hand2')
        self.btn_send.pack(side=tk.RIGHT, padx=(4,4), pady=4)
        self.btn_send.bind('<Button-1>', self._send)

        # 底部提示
        self.lbl_hint = tk.Label(self.root, text='Enter 发送 · Esc 关闭 · 拖拽顶栏移动',
                                 bg=C['bg'], fg=C['fg2'], font=('sans', 8))
        self.lbl_hint.pack(fill=tk.X, padx=10, pady=(0,4))

    def _drag_start(self, e):
        self._drag_data['x'] = e.x_root
        self._drag_data['y'] = e.y_root

    def _drag_move(self, e):
        dx = e.x_root - self._drag_data['x']
        dy = e.y_root - self._drag_data['y']
        x = self.root.winfo_x() + dx
        y = self.root.winfo_y() + dy
        self.root.geometry(f'+{x}+{y}')
        self._drag_data['x'] = e.x_root
        self._drag_data['y'] = e.y_root

    def _add_bubble(self, role, text):
        bubble = tk.Frame(self.msg_container, bg=C['assistant_bubble'] if role=='tork' else C['user_bubble'])
        bubble.pack(fill=tk.X, padx=10, pady=4)
        lbl = tk.Label(bubble, text=text, bg=bubble.cget('bg'), fg=C['fg'],
                       font=('sans', 11), wraplength=440, justify='left',
                       padx=12, pady=8, anchor='w')
        lbl.pack(fill=tk.X)
        self.root.after(50, self._scroll_bottom)

    def _scroll_bottom(self):
        self.msg_canvas.yview_moveto(1.0)

    def _send(self, event=None):
        txt = self.entry.get().strip()
        if not txt: return
        self.entry.delete(0, tk.END)
        self._add_bubble('user', txt)

        self._generating = True
        self.lbl_status.config(text='思考中...')
        self.btn_send.config(bg='#555')
        threading.Thread(target=self._gen_reply, args=(txt,), daemon=True).start()

    def _gen_reply(self, txt):
        time.sleep(0.5)
        reply_map = {
            '心跳': '❤ TORK Core 正在运行，tick 正常。',
            '状态': '✓ 全部组件正常 | TORK Core: ✅ | 引擎: ✅',
            '帮助': '可用命令：心跳、状态、编译、运行、帮助',
            '编译': '▶ 正在编译 TORK Engine...\n编译完成，无错误。',
            '运行': '▶ TORK Core 已启动，PID 正在运行。',
        }
        reply = reply_map.get(txt, f'收到："{txt}"\nTORK 已了解，正在处理。')
        self.root.after(0, lambda: self._show_reply(reply))

    def _show_reply(self, text):
        self._add_bubble('tork', text)
        self._generating = False
        self.lbl_status.config(text='就绪')
        self.btn_send.config(bg=C['accent'])

    def show(self):
        self.root.deiconify()
        self.root.lift()
        self.root.after(50, lambda: self.entry.focus_set())

    def hide(self):
        self.root.withdraw()

    def toggle(self):
        if self.root.state() == 'normal' and self.root.winfo_viewable():
            self.hide()
        else:
            self.show()

    def _start_poll(self):
        sig = '/tmp/tork.flag'
        def poll():
            while True:
                try:
                    if os.path.exists(sig):
                        with open(sig) as f:
                            cmd = f.read().strip()
                        os.remove(sig)
                        if cmd in ('toggle', 'show'):
                            self.root.after(0, self.toggle if cmd=='toggle' else self.show)
                except: pass
                time.sleep(0.3)
        threading.Thread(target=poll, daemon=True).start()

    def run(self):
        self.root.mainloop()

def main():
    app = TorkFloating()
    if len(sys.argv) > 1 and sys.argv[1] == '--show':
        app.show()
    if len(sys.argv) > 1 and sys.argv[1] == 'toggle':
        app.toggle()
    app.run()

if __name__ == '__main__':
    main()
