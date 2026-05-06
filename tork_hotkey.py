#!/usr/bin/env python3
"""
TORK 全局热键守护进程 (Wayland 原生版)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
使用 python-evdev 直接读取 /dev/input/ 事件
支持 Wayland/X11，无需 sudo（需 input 组权限）
"""

import os, sys, time, signal, selectors, threading
from evdev import InputDevice, ecodes, list_devices, categorize

BASE = os.path.dirname(os.path.abspath(__file__))
PID_FILE = '/tmp/tork_hotkey.pid'
SIGNAL_FILE = '/tmp/tork.flag'

class TorkHotkeyDaemon:
    def __init__(self):
        self._running = True
        # 按键状态
        self._ctrl = False
        self._shift = False
        self._last_toggle = 0
        
        # 需要排除的设备（鼠标的键盘接口等）
        self._exclude_names = [
            'mouse', 'gaming', 'g402', 'g502', 'system control',
            'consumer control', 'power', 'video bus'
        ]

    def _find_keyboards(self):
        """找到真正的键盘设备"""
        keyboards = []
        try:
            for path in list_devices():
                dev = InputDevice(path)
                name_lower = dev.name.lower()
                # 排除非键盘设备
                if any(x in name_lower for x in self._exclude_names):
                    dev.close()
                    continue
                # 只有 Keyboard 或包含 keyboard 描述的
                if 'keyboard' in name_lower or 'kbd' in dev.phys.lower():
                    keyboards.append(dev)
                    print(f"   📋 {dev.path} {dev.name}", flush=True)
                else:
                    dev.close()
        except Exception as e:
            print(f"   ⚠ 设备扫描: {e}", flush=True)
        return keyboards

    def _send_signal(self, cmd='toggle'):
        try:
            with open(SIGNAL_FILE, 'w') as f:
                f.write(cmd)
        except Exception:
            pass

    def run(self):
        print(f"🦀 TORK 热键守护进程 (PID {os.getpid()})", flush=True)
        
        # 保存 PID
        with open(PID_FILE, 'w') as f:
            f.write(str(os.getpid()))
        
        # 找到键盘设备
        keyboards = self._find_keyboards()
        if not keyboards:
            print("   ❌ 未找到键盘设备，安装 evdev：pip install evdev", flush=True)
            sys.exit(1)
        
        print(f"   已发现 {len(keyboards)} 个键盘设备", flush=True)
        print(f"   热键: 左 Ctrl + 左 Shift + T → 唤出浮窗", flush=True)
        print(f"   按 Ctrl+C 停止", flush=True)

        # 使用 selector 多路复用
        sel = selectors.DefaultSelector()
        for kb in keyboards:
            sel.register(kb, selectors.EVENT_READ)
        
        try:
            while self._running and keyboards:
                for key, _ in sel.select(timeout=0.5):
                    dev = key.fileobj
                    try:
                        for event in dev.read():
                            if event.type != ecodes.EV_KEY:
                                continue
                            kev = categorize(event)
                            code = event.code
                            val = event.value  # 0=release, 1=press, 2=repeat
                            
                            # 跟踪 Ctrl
                            if code == ecodes.KEY_LEFTCTRL:
                                self._ctrl = (val == 1)
                            elif code == ecodes.KEY_RIGHTCTRL:
                                self._ctrl = (val == 1)
                            # 跟踪 Shift
                            elif code == ecodes.KEY_LEFTSHIFT:
                                self._shift = (val == 1)
                            elif code == ecodes.KEY_RIGHTSHIFT:
                                self._shift = (val == 1)
                            # T 键按下时检测
                            elif code == ecodes.KEY_T and val == 1:
                                if self._ctrl and self._shift:
                                    now = time.time()
                                    if now - self._last_toggle > 0.5:  # 防抖
                                        self._last_toggle = now
                                        print(f"   🔔 Ctrl+Shift+T 触发", flush=True)
                                        self._send_signal('toggle')
                    except (BlockingIOError, OSError):
                        pass
        except KeyboardInterrupt:
            pass
        finally:
            for kb in keyboards:
                kb.close()
            print("\nTORK 热键守护进程已停止", flush=True)

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

if __name__ == '__main__':
    main()
