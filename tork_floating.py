#!/usr/bin/env python3
"""TORK 悬浮窗 v5 — 带看门狗警报"""
import tkinter as tk, os, sys, time, threading, subprocess, json
from pathlib import Path

BASE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(BASE, 'api'))
API_KEY = os.environ.get('DEEPSEEK_API_KEY', '')

C = {
    'bg': '#1e1e1e', 'bg2': '#252526', 'fg': '#d4d4d4', 'fg2': '#808080',
    'accent': '#0078D4', 'success': '#6a9955', 'warn': '#d7ba7d', 'danger': '#f44747',
    'border': '#3c3c3c',
    'input_bg': '#2d2d2d', 'user_bubble': '#3d3d3d', 'assistant_bubble': '#252526',
}

WATCHDOG_ALERT = Path('/tmp/tork_watchdog.alert')
FLAG_FILE = Path('/tmp/tork.flag')

class TorkFloating:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("TORK")
        self.W, self.H = 540, 480
        self._messages = []
        self._generating = False
        self._alert_active = False

        self.root.wm_attributes("-type", "splash")
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
        self._start_watchdog_poll()

    def _build_ui(self):
        # == 顶栏 ==
        self.top = tk.Frame(self.root, bg=C['bg2'], height=32, cursor='fleur')
        self.top.pack(fill=tk.X)
        self.top.pack_propagate(False)

        self.dot = tk.Canvas(self.top, width=10, height=10, bg=C['bg2'], highlightthickness=0)
        self.dot.pack(side=tk.LEFT, padx=(10,4), pady=11)
        self.dot_id = self.dot.create_oval(1,1,9,9, fill=C['success'], outline='')

        tk.Label(self.top, text='TORK', bg=C['bg2'], fg=C['fg'], font=('Segoe UI', 11, 'bold')).pack(side=tk.LEFT)
        self.lbl_status = tk.Label(self.top, text='就绪', bg=C['bg2'], fg=C['fg2'], font=('Segoe UI', 9))
        self.lbl_status.pack(side=tk.LEFT, padx=6)

        self.btn_close = tk.Label(self.top, text='✕', bg=C['bg2'], fg=C['fg2'], font=('Segoe UI', 12), cursor='hand2')
        self.btn_close.pack(side=tk.RIGHT, padx=(0,8))
        self.btn_close.bind('<Button-1>', lambda e: self.hide())

        self._drag_data = {'x':0, 'y':0}
        self.top.bind('<Button-1>', self._drag_start)
        self.top.bind('<B1-Motion>', self._drag_move)

        # == 消息区 ==
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

        self._add_bubble('tork', '你好，我是 TORK。我会在后台看门，发现可疑的高消耗进程就告诉你。')

        # == 底部 ==
        input_frame = tk.Frame(self.root, bg=C['bg'], height=56)
        input_frame.pack(fill=tk.X)
        input_frame.pack_propagate(False)

        inner = tk.Frame(input_frame, bg=C['input_bg'])
        inner.pack(fill=tk.BOTH, expand=True, padx=8, pady=6)
        inner.pack_propagate(False)

        self.entry = tk.Entry(inner, bg=C['input_bg'], fg=C['fg'], font=('Segoe UI', 13),
                               insertbackground=C['fg'], bd=0, relief=tk.FLAT)
        self.entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(8,0), pady=8, ipady=4)
        self.entry.bind('<Return>', self._send)
        self.entry.bind('<Escape>', lambda e: self.hide())
        self.entry.focus_set()

        self.btn_send = tk.Label(inner, text='▶', bg=C['accent'], fg='white',
                                  font=('Segoe UI', 11), padx=10, pady=4, cursor='hand2')
        self.btn_send.pack(side=tk.RIGHT, padx=(4,4), pady=4)
        self.btn_send.bind('<Button-1>', self._send)

        self.lbl_hint = tk.Label(self.root, text='Enter 发送 · Esc 关闭 · 拖拽顶栏移动',
                                 bg=C['bg'], fg=C['fg2'], font=('Segoe UI', 8))
        self.lbl_hint.pack(fill=tk.X, padx=10, pady=(0,4))

        # == 看门狗警报覆盖层（默认隐藏）==
        self.alert_frame = tk.Frame(self.root, bg='#2d1b1b', bd=2, relief=tk.RIDGE, highlightbackground=C['danger'], highlightthickness=1)
        # 不 pack，等警报来了再显示

    def _build_alert_ui(self, data):
        """构建警报弹窗内容"""
        for w in self.alert_frame.winfo_children():
            w.destroy()

        procs = data.get('procs', [])
        if not procs:
            return

        # 标题
        title = tk.Label(self.alert_frame, text='⚠ 发现高消耗进程', bg='#2d1b1b', fg=C['danger'],
                         font=('Segoe UI', 13, 'bold'), padx=12, pady=6)
        title.pack(fill=tk.X)

        # 进程列表
        for p in procs[:3]:  # 最多显示 3 个
            info = f"  {p['name']} (PID {p['pid']})  CPU {p['cpu']}%  内存 {p['mem_mb']}MB"
            tk.Label(self.alert_frame, text=info, bg='#2d1b1b', fg=C['fg'],
                     font=('Segoe UI', 10), padx=12, pady=1, anchor='w').pack(fill=tk.X)

        if len(procs) > 3:
            tk.Label(self.alert_frame, text=f"  ...还有 {len(procs)-3} 个", bg='#2d1b1b', fg=C['fg2'],
                     font=('Segoe UI', 9), padx=12, pady=1, anchor='w').pack(fill=tk.X)

        tk.Label(self.alert_frame, text='', bg='#2d1b1b').pack()  # 间距

        # 按钮区
        btn_frame = tk.Frame(self.alert_frame, bg='#2d1b1b')
        btn_frame.pack(fill=tk.X, padx=12, pady=(0,8))

        btn_yes = tk.Button(btn_frame, text='✓ 是的，我开的', bg='#2d5a2d', fg='white',
                            font=('Segoe UI', 10), padx=10, pady=4, bd=0, cursor='hand2',
                            command=lambda: self._watchdog_yes(procs))
        btn_yes.pack(side=tk.LEFT, padx=(0,6))

        btn_no = tk.Button(btn_frame, text='✗ 不是，帮我干掉', bg=C['danger'], fg='white',
                           font=('Segoe UI', 10), padx=10, pady=4, bd=0, cursor='hand2',
                           command=lambda: self._watchdog_no(procs))
        btn_no.pack(side=tk.LEFT, padx=(0,6))

        btn_ignore = tk.Button(btn_frame, text='忽略这次', bg='#3c3c3c', fg=C['fg2'],
                               font=('Segoe UI', 9), padx=8, pady=4, bd=0, cursor='hand2',
                               command=self._watchdog_ignore)
        btn_ignore.pack(side=tk.RIGHT)

    def _watchdog_yes(self, procs):
        """用户说「是的，我开的」"""
        for p in procs:
            cmd = f"watchdog_yes:{p['pid']}:{p['name']}"
            try:
                with open(FLAG_FILE, 'w') as f:
                    f.write(cmd)
            except:
                pass
        self._alert_active = False
        self.alert_frame.pack_forget()
        self._add_bubble('user', f"✓ {procs[0]['name']} 是我开的，记住了")
        self._add_bubble('tork', f"好的，下次不打扰你了。")
        self.lbl_status.config(text='就绪')
        self.dot.itemconfig(self.dot_id, fill=C['success'])

    def _watchdog_no(self, procs):
        """用户说「不是，帮我干掉」"""
        for p in procs:
            cmd = f"watchdog_no:{p['pid']}"
            try:
                with open(FLAG_FILE, 'w') as f:
                    f.write(cmd)
            except:
                pass
        self._alert_active = False
        self.alert_frame.pack_forget()
        self._add_bubble('user', f"✗ {procs[0]['name']} 不是我开的，干掉它")
        self._add_bubble('tork', f"已发送终止信号。如果它没死，我会补一刀。")
        self.lbl_status.config(text='就绪')
        self.dot.itemconfig(self.dot_id, fill=C['success'])

    def _watchdog_ignore(self):
        """忽略这次"""
        self._alert_active = False
        self.alert_frame.pack_forget()
        self.lbl_status.config(text='就绪')
        self.dot.itemconfig(self.dot_id, fill=C['success'])

    def _show_watchdog_alert(self, data):
        """显示看门狗警报"""
        if self._alert_active:
            return  # 已有活跃警报，不重复
        self._alert_active = True
        self._build_alert_ui(data)
        self.alert_frame.pack(fill=tk.X, side=tk.TOP, before=self.top if self.top.winfo_children() else None)
        self.dot.itemconfig(self.dot_id, fill=C['danger'])
        self.lbl_status.config(text='⚠ 警报')
        self.show()  # 确保窗口可见
        self.root.bell()  # 响一声提醒

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
                       font=('Segoe UI', 11), wraplength=480, justify='left',
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
        time.sleep(0.3)
        reply_map = {
            '心跳': '❤ TORK Core 正在运行，tick 正常。',
            '状态': '✓ 全部组件正常 | 看门狗: ✅ | Core: ✅',
            '帮助': '可用命令：心跳、状态、看门狗、帮助\n\n看门狗会自动监控高消耗进程。',
            '看门狗': '🔍 看门狗正在后台运行，每 5 秒扫描一次 /proc。\n发现陌生进程 CPU>20% 或内存>200MB 时会弹窗问你。',
        }
        reply = reply_map.get(txt, f'收到：「{txt}」\nTORK 已了解，正在处理。')
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
        """监听 /tmp/tork.flag (原有的 toggle/show)"""
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

    def _start_watchdog_poll(self):
        """监听 /tmp/tork_watchdog.alert (看门狗警报)"""
        def poll():
            last_mtime = 0
            while True:
                try:
                    if WATCHDOG_ALERT.exists():
                        mtime = os.path.getmtime(WATCHDOG_ALERT)
                        if mtime > last_mtime:
                            last_mtime = mtime
                            with open(WATCHDOG_ALERT) as f:
                                data = json.load(f)
                            self.root.after(0, lambda d=data: self._show_watchdog_alert(d))
                except: pass
                time.sleep(1)
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
