#!/usr/bin/env python3
"""
TORK 全局热键守护进程
━━━━━━━━━━━━━━━━━━━━
后台监听 Ctrl+Shift+T，按下时通过信号文件唤出 TORK 悬浮窗。
"""

import subprocess, os, sys, time, threading, signal, json, re

BASE = os.path.dirname(os.path.abspath(__file__))
PID_FILE = '/tmp/tork_hotkey.pid'
CONFIG_FILE = os.path.join(BASE, '.tork_floating.json')
SIGNAL_FILE = '/tmp/tork.flag'

# ── 热键配置 ──
HOTKEY_MODS = {'Control_L', 'Control_R'}
HOTKEY_KEY = 'T'

# ── 按键名映射（xinput 输出的 key label -> 我们的逻辑名） ──
KEY_MAP = {
    't': 'T', 'T': 'T',
    'Control_L': 'Control_L', 'Control_R': 'Control_R',
    'Shift_L': 'Shift_L', 'Shift_R': 'Shift_R',
    'Alt_L': 'Alt_L', 'Alt_R': 'Alt_R',
    'Super_L': 'Super_L', 'Super_R': 'Super_R',
}

class TorkHotkeyDaemon:
    def __init__(self):
        self._running = True
        self._pressed = set()
        self._debounce = 0

    def _get_keyboard_ids(self):
        """找到所有键盘设备 ID"""
        ids = []
        try:
            out = subprocess.run(['xinput', 'list'], capture_output=True, text=True, timeout=3)
            for line in out.stdout.split('\n'):
                if ('keyboard' in line.lower() or 'Keyboard' in line) and 'slave' in line.lower():
                    m = re.search(r'id=(\d+)', line)
                    if m:
                        ids.append(m.group(1))
        except: pass
        return ids

    def _send_signal(self, cmd):
        """通过信号文件通知浮窗"""
        try:
            with open(SIGNAL_FILE, 'w') as f:
                f.write(cmd)
        except: pass

    def run(self):
        with open(PID_FILE, 'w') as f:
            f.write(str(os.getpid()))

        print(f"🦀 TORK 热键守护进程 (PID {os.getpid()})")
        print(f"   热键: Ctrl+Shift+T → 唤出悬浮窗")
        print(f"   按 Ctrl+C 停止")

        kb_ids = self._get_keyboard_ids()
        if not kb_ids:
            print("   ⚠ 未找到键盘设备，fallback 到 id=13")
            kb_ids = ['13']

        # 同时监听所有键盘设备
        procs = []
        for kid in kb_ids:
            try:
                p = subprocess.Popen(
                    ['xinput', 'test', kid],
                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                    text=True, bufsize=1
                )
                procs.append(p)
            except: pass

        if not procs:
            print("   ❌ 无法监听键盘，使用轮询模式")
            self._polling_mode()
            return

        # 读取所有设备输出
        import selectors
        sel = selectors.DefaultSelector()
        for p in procs:
            if p.stdout:
                sel.register(p.stdout, selectors.EVENT_READ)

        while self._running and procs:
            for key, _ in sel.select(timeout=0.5):
                line = key.fileobj.readline()
                if not line:
                    continue
                line = line.strip()
                if 'key press' in line:
                    parts = line.split()
                    if len(parts) >= 3:
                        k = parts[-1]
                        mapped = KEY_MAP.get(k, k)
                        self._pressed.add(mapped)
                        self._check_hotkey()
                elif 'key release' in line:
                    parts = line.split()
                    if len(parts) >= 3:
                        k = parts[-1]
                        mapped = KEY_MAP.get(k, k)
                        self._pressed.discard(mapped)

        for p in procs:
            p.terminate()

    def _polling_mode(self):
        """fallback: 用 xinput query-state 轮询"""
        kb_ids = self._get_keyboard_ids()
        if not kb_ids:
            kb_ids = ['13']
        while self._running:
            try:
                for kid in kb_ids:
                    out = subprocess.run(
                        ['xinput', 'query-state', kid],
                        capture_output=True, text=True, timeout=1
                    )
                    self._check_hotkey()
            except: pass
            time.sleep(0.3)

    def _check_hotkey(self):
        now = time.time()
        if now - self._debounce < 0.3:
            return
        ctrl_down = any(m in self._pressed for m in ['Control_L', 'Control_R'])
        shift_down = any(m in self._pressed for m in ['Shift_L', 'Shift_R'])
        t_down = 'T' in self._pressed
        if ctrl_down and shift_down and t_down:
            self._debounce = now
            self._send_signal('toggle')
            self._pressed.clear()

    def stop(self):
        self._running = False
        if os.path.exists(PID_FILE):
            os.unlink(PID_FILE)


def main():
    daemon = TorkHotkeyDaemon()
    try:
        daemon.run()
    except KeyboardInterrupt:
        daemon.stop()
        print("\nTORK 热键守护进程已停止")

if __name__ == '__main__':
    main()
