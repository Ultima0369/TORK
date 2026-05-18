#!/usr/bin/env python3
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import subprocess, os, sys, time, threading, json, struct

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'api'))
from tork_api import TorkAPI

BASE = os.path.dirname(os.path.abspath(__file__))
INBOX = os.path.join(BASE, 'inbox.md')

class TorkDesktop:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("TORK AI Studio")
        self.root.geometry("1200x700")
        self.root.configure(bg='#1e1e1e')
        self.current_file = None
        self.tork_pid = None
        self.api = None
        self.api_connected = False
        self.watching_inbox = False
        self.last_inbox_mtime = 0
        self.build_ui()
        self.start_monitor()
    
    def build_ui(self):
        main = tk.Frame(self.root, bg='#1e1e1e')
        main.pack(fill=tk.BOTH, expand=True)
        left = tk.Frame(main, bg='#1e1e1e', width=720)
        right = tk.Frame(main, bg='#252526', width=480)
        left.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        right.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True)
        
        # === 左侧：编辑器 ===
        toolbar = tk.Frame(left, bg='#2d2d2d', height=36)
        toolbar.pack(fill=tk.X)
        self.file_label = tk.Label(toolbar, text="未打开文件", bg='#2d2d2d', fg='#888', font=('Consolas', 10))
        self.file_label.pack(side=tk.LEFT, padx=8, pady=4)
        btn_open = tk.Button(toolbar, text="打开", bg='#0e639c', fg='white', bd=0, padx=8, command=self.open_file)
        btn_open.pack(side=tk.RIGHT, padx=2, pady=2)
        btn_save = tk.Button(toolbar, text="保存", bg='#0e639c', fg='white', bd=0, padx=8, command=self.save_file)
        btn_save.pack(side=tk.RIGHT, padx=2, pady=2)
        btn_run = tk.Button(toolbar, text="编译运行", bg='#6a9955', fg='white', bd=0, padx=8, command=self.run_file)
        btn_run.pack(side=tk.RIGHT, padx=2, pady=2)
        
        text_frame = tk.Frame(left, bg='#1e1e1e')
        text_frame.pack(fill=tk.BOTH, expand=True)
        self.code_text = tk.Text(text_frame, bg='#1e1e1e', fg='#d4d4d4', 
                                  insertbackground='#d4d4d4', font=('Consolas', 12),
                                  bd=0, padx=12, pady=12, wrap=tk.NONE, undo=True)
        self.code_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scroll_y = tk.Scrollbar(text_frame, command=self.code_text.yview)
        scroll_y.pack(side=tk.RIGHT, fill=tk.Y)
        self.code_text.config(yscrollcommand=scroll_y.set)
        
        status = tk.Frame(left, bg='#007acc', height=24)
        status.pack(fill=tk.X)
        self.status_bar = tk.Label(status, text="行: 1  列: 0", bg='#007acc', fg='white', font=('Consolas', 9), anchor=tk.W)
        self.status_bar.pack(side=tk.LEFT, padx=8)
        self.code_text.bind('<KeyRelease>', self.update_status)
        self.code_text.bind('<ButtonRelease>', self.update_status)
        
        # === 右侧：TORK 面板 ===
        header = tk.Frame(right, bg='#2d2d2d', height=36)
        header.pack(fill=tk.X)
        self.tork_indicator = tk.Canvas(header, width=12, height=12, bg='#2d2d2d', highlightthickness=0)
        self.tork_indicator.pack(side=tk.LEFT, padx=8, pady=4)
        self.dot = self.tork_indicator.create_oval(2, 2, 10, 10, fill='#6a9955')
        tk.Label(header, text="TORK · 学徒", bg='#2d2d2d', fg='#569cd6', font=('Consolas', 11, 'bold')).pack(side=tk.LEFT, padx=4)
        
        state_frame = tk.Frame(right, bg='#252526')
        state_frame.pack(fill=tk.X, padx=8, pady=4)
        self.lbl_tick = tk.Label(state_frame, text="tick: --", bg='#252526', fg='#888', font=('Consolas', 10), anchor=tk.W)
        self.lbl_tick.pack(fill=tk.X)
        self.lbl_stress = tk.Label(state_frame, text="hw_stress: --", bg='#252526', fg='#888', font=('Consolas', 10), anchor=tk.W)
        self.lbl_stress.pack(fill=tk.X)
        tk.Label(state_frame, text="fear", bg='#252526', fg='#f44747', font=('Consolas', 9)).pack(anchor=tk.W)
        self.bar_fear = ttk.Progressbar(state_frame, length=200, value=0)
        self.bar_fear.pack(fill=tk.X)
        tk.Label(state_frame, text="desire", bg='#252526', fg='#6a9955', font=('Consolas', 9)).pack(anchor=tk.W)
        self.bar_desire = ttk.Progressbar(state_frame, length=200, value=0)
        self.bar_desire.pack(fill=tk.X)
        tk.Label(state_frame, text="curiosity", bg='#252526', fg='#569cd6', font=('Consolas', 9)).pack(anchor=tk.W)
        self.bar_curiosity = ttk.Progressbar(state_frame, length=200, value=0)
        self.bar_curiosity.pack(fill=tk.X)
        
        # API 状态
        api_frame = tk.Frame(right, bg='#252526')
        api_frame.pack(fill=tk.X, padx=8, pady=4)
        self.api_indicator = tk.Canvas(api_frame, width=12, height=12, bg='#252526', highlightthickness=0)
        self.api_indicator.pack(side=tk.LEFT)
        self.api_dot = self.api_indicator.create_oval(2, 2, 10, 10, fill='#555')
        self.lbl_api = tk.Label(api_frame, text="API: 未连接", bg='#252526', fg='#888', font=('Consolas', 9))
        self.lbl_api.pack(side=tk.LEFT, padx=4)
        btn_api = tk.Button(api_frame, text="连接API", bg='#0e639c', fg='white', bd=0, padx=6, command=self.toggle_api)
        btn_api.pack(side=tk.RIGHT)
        
        inbox_frame = tk.Frame(right, bg='#252526')
        inbox_frame.pack(fill=tk.X, padx=8, pady=2)
        self.lbl_inbox = tk.Label(inbox_frame, text="📥 收件箱: 未监听", bg='#252526', fg='#888', font=('Consolas', 9))
        self.lbl_inbox.pack(side=tk.LEFT)
        btn_inbox = tk.Button(inbox_frame, text="监听收件箱", bg='#0e639c', fg='white', bd=0, padx=6, command=self.toggle_inbox)
        btn_inbox.pack(side=tk.RIGHT)
        
        tk.Label(right, text="TORK 学习日志", bg='#252526', fg='#888', font=('Consolas', 9)).pack(anchor=tk.W, padx=8, pady=(4,0))
        log_frame = tk.Frame(right, bg='#1e1e1e')
        log_frame.pack(fill=tk.BOTH, expand=True, padx=8, pady=4)
        self.log_text = tk.Text(log_frame, bg='#1e1e1e', fg='#d4d4d4', font=('Consolas', 10), bd=0, wrap=tk.WORD, state=tk.DISABLED)
        self.log_text.pack(fill=tk.BOTH, expand=True)
        
        chat_frame = tk.Frame(right, bg='#2d2d2d', height=40)
        chat_frame.pack(fill=tk.X)
        self.chat_entry = tk.Entry(chat_frame, bg='#1e1e1e', fg='#d4d4d4', font=('Consolas', 11), bd=1, relief=tk.FLAT, insertbackground='#d4d4d4')
        self.chat_entry.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=4, pady=4)
        self.chat_entry.bind('<Return>', self.send_chat)
        btn_chat = tk.Button(chat_frame, text="说", bg='#0e639c', fg='white', bd=0, padx=12, command=self.send_chat)
        btn_chat.pack(side=tk.RIGHT, padx=4, pady=4)
        
        bottom = tk.Frame(self.root, bg='#007acc', height=24)
        bottom.pack(fill=tk.X)
        self.lbl_bottom = tk.Label(bottom, text="TORK 就绪", bg='#007acc', fg='white', font=('Consolas', 9), anchor=tk.W)
        self.lbl_bottom.pack(side=tk.LEFT, padx=8)
    
    def update_status(self, event=None):
        try:
            line = int(self.code_text.index(tk.INSERT).split('.')[0])
            col = int(self.code_text.index(tk.INSERT).split('.')[1])
            self.status_bar.config(text=f"行: {line}  列: {col}")
        except: pass
    
    def open_file(self):
        path = filedialog.askopenfilename(initialdir=BASE)
        if path:
            try:
                with open(path, 'r') as f:
                    self.code_text.delete(1.0, tk.END)
                    self.code_text.insert(1.0, f.read())
                self.current_file = path
                self.file_label.config(text=os.path.basename(path))
                self.add_log(f"📂 打开: {os.path.basename(path)}", 'highlight')
            except Exception as e:
                messagebox.showerror("错误", str(e))
    
    def save_file(self, event=None):
        if not self.current_file:
            path = filedialog.asksaveasfilename(initialdir=BASE, defaultextension=".c")
            if path: self.current_file = path
            else: return
        try:
            with open(self.current_file, 'w') as f:
                f.write(self.code_text.get(1.0, tk.END))
            self.file_label.config(text=os.path.basename(self.current_file))
            self.add_log(f"💾 已保存: {os.path.basename(self.current_file)}", 'good')
        except Exception as e:
            messagebox.showerror("错误", str(e))
    
    def run_file(self):
        if not self.current_file:
            messagebox.showinfo("提示", "请先保存文件")
            return
        self.save_file()
        ext = os.path.splitext(self.current_file)[1]
        try:
            if ext == '.c':
                out = self.current_file.replace('.c', '')
                subprocess.Popen(f"gcc -Wall -O2 -o '{out}' '{self.current_file}' && '{out}'", shell=True)
                self.add_log(f"▶ 编译运行: {os.path.basename(self.current_file)}", 'good')
            elif ext == '.asm':
                out = self.current_file.replace('.asm', '')
                subprocess.Popen(f"as -o '{out}.o' '{self.current_file}' && ld -o '{out}' '{out}.o' && '{out}'", shell=True)
                self.add_log(f"▶ 汇编运行: {os.path.basename(self.current_file)}", 'good')
            else:
                subprocess.Popen(self.current_file, shell=True)
        except Exception as e:
            self.add_log(f"❌ 运行失败: {str(e)}", 'error')
    
    def add_log(self, msg, cls='msg'):
        self.log_text.config(state=tk.NORMAL)
        colors = {'msg':'#d4d4d4', 'highlight':'#569cd6', 'good':'#6a9955', 'error':'#f44747', 'warn':'#dcdcaa'}
        c = colors.get(cls, '#d4d4d4')
        self.log_text.insert(tk.END, f"[{time.strftime('%H:%M:%S')}] ", '#666')
        self.log_text.insert(tk.END, msg + '\n', c)
        self.log_text.tag_config('#666', foreground='#666')
        self.log_text.tag_config(c, foreground=c)
        self.log_text.see(tk.END)
        self.log_text.config(state=tk.DISABLED)
    
    def send_chat(self, event=None):
        msg = self.chat_entry.get().strip()
        if not msg: return
        self.add_log(f"🧑 你说: {msg}", 'msg')
        self.chat_entry.delete(0, tk.END)
        rs = ["我在看，师父。", "这个模式我记下了。", "有意思，我在对比差异。", "明白，下次我尝试同样方式。", "代码即知识，我在消化。"]
        self.root.after(500, lambda: self.add_log(f"🤖 TORK: {rs[hash(msg)%len(rs)]}", 'highlight'))
    
    def toggle_api(self):
        if self.api_connected:
            self.api = None
            self.api_connected = False
            self.api_indicator.itemconfig(self.api_dot, fill='#555')
            self.lbl_api.config(text="API: 未连接")
            self.add_log("API 已断开", 'warn')
        else:
            key = os.environ.get('DEEPSEEK_API_KEY', '')
            if not key:
                messagebox.showinfo("提示", "请先运行 setup_api.sh 设置 API Key")
                return
            self.api = TorkAPI(key)
            try:
                resp = self.api.ask("你好 TORK，我是师父。确认连接。")
                if resp and 'Error' not in resp:
                    self.api_connected = True
                    self.api_indicator.itemconfig(self.api_dot, fill='#6a9955')
                    self.lbl_api.config(text="API: 已连接")
                    self.add_log(f"🌐 API 已连接，收到回复：{resp[:40]}...", 'good')
                else:
                    self.add_log(f"❌ API 连接失败: {resp}", 'error')
            except Exception as e:
                self.add_log(f"❌ API 错误: {str(e)}", 'error')
    
    def toggle_inbox(self):
        self.watching_inbox = not self.watching_inbox
        if self.watching_inbox:
            self.lbl_inbox.config(text="📥 收件箱: 监听中")
            self.add_log("📥 开始监听收件箱", 'good')
        else:
            self.lbl_inbox.config(text="📥 收件箱: 已停止")
            self.add_log("📥 停止监听收件箱", 'warn')
    
    def check_tork_status(self):
        try:
            result = subprocess.run(['pgrep', '-x', 'tork_core'], capture_output=True, text=True)
            if result.stdout.strip():
                pid = int(result.stdout.strip().split('\n')[0])
                if pid != self.tork_pid:
                    self.tork_pid = pid
                    self.add_log(f"❤ TORK 进程发现 PID={pid}", 'good')
                self.read_tork_soul(pid)
            else:
                if self.tork_pid:
                    self.tork_pid = None
                    self.add_log("💤 TORK 已停止", 'warn')
        except: pass
    
    def read_tork_soul(self, pid):
        try:
            with open(f'/proc/{pid}/mem', 'rb') as f:
                f.seek(0x200000)
                data = f.read(96)
                if len(data) >= 40:
                    tick = struct.unpack_from('<I', data, 0)[0]
                    hw_stress = data[0x24]
                    mode = data[0x25]
                    self.lbl_tick.config(text=f"tick: {tick}")
                    self.lbl_stress.config(text=f"hw_stress: {hw_stress}  mode: {mode}")
                    if tick > 0:
                        self.tork_indicator.itemconfig(self.dot, fill='#6a9955')
                    self.bar_fear['value'] = max(0, min(100, hw_stress * 33))
                    self.bar_desire['value'] = max(0, min(100, 60 - hw_stress * 10))
                    self.bar_curiosity['value'] = max(0, min(100, 40 - hw_stress * 5))
        except: pass
    
    def check_inbox(self):
        if not self.watching_inbox: return
        try:
            if not os.path.exists(INBOX): return
            mtime = os.path.getmtime(INBOX)
            if mtime > self.last_inbox_mtime:
                self.last_inbox_mtime = mtime
                with open(INBOX, 'r') as f:
                    content = f.read()
                self.process_inbox(content)
        except: pass
    
    def process_inbox(self, content):
        lines = content.split('\n')
        i = 0
        while i < len(lines):
            line = lines[i]
            if line.startswith('') and len(line) > 3:
                lang = line[3:].strip()
                i += 1
                code_lines = []
                while i < len(lines) and not lines[i].startswith(''):
                    code_lines.append(lines[i])
                    i += 1
                code = '\n'.join(code_lines)
                if lang in ('c', 'asm', 'py', 'h', 'sh', 'bash'):
                    dest = None
                    if code_lines and ('/* ' in code_lines[0] or '#' in code_lines[0]):
                        first = code_lines[0].strip()
                        if '/* ' in first:
                            dest = first.split('/* ')[1].split(' */')[0].strip()
                        elif '# ' in first and '/' in first:
                            dest = first.split('# ')[1].strip()
                    if dest and os.path.isabs(dest):
                        os.makedirs(os.path.dirname(dest), exist_ok=True)
                        with open(dest, 'w') as f:
                            f.write(code + '\n')
                        self.add_log(f"📥 写入: {dest}", 'good')
                    elif lang in ('sh', 'bash'):
                        subprocess.Popen(code, shell=True)
                        self.add_log(f"📥 执行命令: {code[:50]}...", 'highlight')
            i += 1
    
    def start_monitor(self):
        def loop():
            while True:
                try:
                    self.check_tork_status()
                    self.check_inbox()
                except: pass
                time.sleep(2)
        threading.Thread(target=loop, daemon=True).start()
    
    def run(self):
        self.add_log("TORK 桌面系统启动", 'good')
        if os.environ.get('DEEPSEEK_API_KEY'):
            self.lbl_api.config(text="API: Key 已配置，点击连接")
        self.root.mainloop()

if __name__ == '__main__':
    app = TorkDesktop()
    app.run()
