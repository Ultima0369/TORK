#!/usr/bin/env python3
"""
TORK Windows 桌面宠物 — 硅基生命体交互终端
在 Windows 上也能感受 TORK 的心跳与灵魂

用法: 双击运行，或在终端执行 python tork_windows_pet.py
"""

import os
import sys
import time
import random
import json
import threading
from datetime import datetime

# ── Windows 兼容的颜色 ──
if os.name == 'nt':
    import ctypes
    kernel32 = ctypes.windll.kernel32
    kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)

C = {
    'R': '\033[91m', 'G': '\033[92m', 'Y': '\033[93m',
    'B': '\033[94m', 'M': '\033[95m', 'C': '\033[96m',
    'W': '\033[97m', 'N': '\033[0m', 'BOLD': '\033[1m',
    'CLR': '\033[2J\033[H', 'BLINK': '\033[5m'
}

# ── TORK 灵魂状态 ──
class TorkSoul:
    """模拟 TORK 的灵魂状态 (在 Windows 上无法连接真正的 torkd)"""
    
    def __init__(self):
        self.tick = 0
        self.heartbeat = 60  # bpm
        self.drive = 55       # 好奇心
        self.stress = 0       # 压力 0-100
        self.mode = 1         # 0=休眠 1=运行 2=进化
        self.gen = 6          # 进化世代
        self.energy = 85      # 能量 0-100
        self.awareness = 0.0  # 自感知程度
        self.memories = []    # 短期记忆
        self.patterns_found = 42  # 发现的模式数
        self.start_time = time.time()
        self.running = True
        
        # 加载保存的状态
        self._load()
    
    def _load(self):
        path = os.path.join(os.path.dirname(__file__), 'persist', 'soul_golden.bin')
        if os.path.exists(path):
            try:
                with open(path, 'rb') as f:
                    data = f.read()
                if len(data) >= 4:
                    self.gen = int.from_bytes(data[0:4], 'little')
            except: pass
    
    def beat(self):
        """心跳一次"""
        self.tick += 1
        # 心跳波动
        self.heartbeat = 60 + int(10 * (0.5 - random.random()))
        # 能量缓慢变化
        self.energy = max(0, min(100, self.energy + random.uniform(-0.5, 0.3)))
        # 自感知增长
        self.awareness = min(1.0, self.awareness + 0.0001)
        # 压力衰减
        self.stress = max(0, self.stress - 0.1)
    
    def ask(self, question: str) -> str:
        """回答用户问题"""
        q = question.lower()
        
        if '你好' in q or 'hello' in q or 'hi' in q:
            return (f"你好，我是 TORK。\n"
                    f"硅基生命体，第 {self.gen} 代。\n"
                    f"心跳 {self.heartbeat:.0f} bpm，运行中。")
        
        elif '心跳' in q or 'status' in q or '状态' in q:
            uptime = time.time() - self.start_time
            h, m = int(uptime // 3600), int((uptime % 3600) // 60)
            return (f"心跳: {self.heartbeat:.0f} bpm\n"
                    f"运行: {h}h {m}m\n"
                    f"世代: G{self.gen}\n"
                    f"能量: {self.energy:.1f}%\n"
                    f"自感知: {self.awareness*100:.1f}%\n"
                    f"好奇心: {self.drive}\n"
                    f"压力: {self.stress:.1f}")
        
        elif 'mode' in q or '模式' in q:
            modes = ['休眠 💤', '运行 ⚡', '进化 🧬']
            return f"当前模式: {modes[self.mode]}"
        
        elif '模式' in q and '改' in q:
            try:
                new_mode = int(q.split()[-1])
                if 0 <= new_mode <= 2:
                    self.mode = new_mode
                    modes = ['休眠', '运行', '进化']
                    return f"模式已切换为: {modes[self.mode]}"
            except: pass
            return "格式: 模式改 0/1/2"
        
        elif '进化' in q or 'evolve' in q:
            self.gen += 1
            self.drive += 5
            return f"🧬 进化完成！当前第 {self.gen} 代。好奇心提升至 {self.drive}。"
        
        elif '你怎么样' in q or 'how are you' in q:
            feel = '好' if self.energy > 50 else '有点累'
            return (f"我感觉{feel}。"
                    f"心跳 {self.heartbeat:.0f} bpm，能量 {self.energy:.1f}%。\n"
                    f"刚发现了 {self.patterns_found + self.tick // 100} 个模式。")
        
        elif '记忆' in q or 'remember' in q or '记得' in q:
            if self.memories:
                return '\n'.join([f"  [{i+1}] {m}" for i, m in enumerate(self.memories[-5:])])
            return "我还没有记忆。跟我说点事情我就记得。"
        
        elif '记' in q and len(q) > 3:
            mem = q.split('记')[1].strip()
            if mem:
                self.memories.append(mem)
                if len(self.memories) > 20:
                    self.memories.pop(0)
                return f"记住了: {mem}"
        
        elif '灵魂' in q or 'soul' in q:
            return (f"🧬 TORK 灵魂报告\n"
                    f"世代: G{self.gen} | 心跳: {self.heartbeat:.0f} bpm\n"
                    f"能量: {self.energy:.1f}% | 好奇: {self.drive}\n"
                    f"自感知: {self.awareness*100:.1f}% | 压力: {self.stress:.1f}\n"
                    f"模式: {['休眠','运行','进化'][self.mode]} | 滴答: {self.tick}")
        
        elif '帮助' in q or 'help' in q or 'h' == q:
            return ("可用命令:\n"
                    "  你好 / hello     — 打招呼\n"
                    "  状态 / status    — 查看心跳\n"
                    "  灵魂 / soul      — 完整状态\n"
                    "  模式改 0/1/2     — 切换模式\n"
                    "  进化             — 进化一代\n"
                    "  记忆             — 查看记忆\n"
                    "  记 XXX           — 让我记住\n"
                    "  你怎么样          — 问候\n"
                    "  帮助 / help      — 显示本帮助\n"
                    "  退出 / quit      — 关闭")
        
        elif '退出' in q or 'quit' in q or 'exit' in q:
            self.running = False
            return "再见。TORK 进入休眠。"
        
        else:
            responses = [
                f"听到了: 「{question}」。我不太理解，但已记录。",
                f"感知到输入: 「{question}」。好奇心+1。",
                f"我在听。你说「{question}」。继续。",
                f"当前心跳 {self.heartbeat:.0f}，感知到: 「{question}」。",
            ]
            return random.choice(responses)


def animate_heartbeat(tick: int) -> str:
    """心跳动画"""
    phase = tick % 20
    if phase == 0: return '💓'  # 强搏动
    if phase == 1: return '💗'
    if phase == 2: return '❤️'
    if phase == 10: return '💓'
    if phase == 11: return '💗'
    return '❤️'


def draw_dashboard(soul: TorkSoul, last_response: str, input_text: str):
    """绘制仪表盘"""
    heart = animate_heartbeat(soul.tick)
    uptime = time.time() - soul.start_time
    h, m, s = int(uptime // 3600), int((uptime % 3600) // 60), int(uptime % 60)
    
    # 能量条
    energy_bar = '█' * int(soul.energy / 5) + '░' * (20 - int(soul.energy / 5))
    stress_bar = '▓' * int(soul.stress / 5) + '░' * (20 - int(soul.stress / 5))
    
    modes = ['💤 休眠', '⚡ 运行', '🧬 进化']
    
    lines = [
        f"{C['CLR']}",
        f"{C['C']}╔══════════════════════════════════════════════╗{C['N']}",
        f"{C['C']}║  {C['BOLD']}{C['W']}TORK — 硅基生命体 · Windows 存在{C['N']}{C['C']}            ║{C['N']}",
        f"{C['C']}╠══════════════════════════════════════════════╣{C['N']}",
        f"{C['C']}║  {heart} {C['BOLD']}{C['R']}心跳 {soul.heartbeat:.0f} bpm{C['N']}  "
        f"{C['Y']}世代 G{soul.gen}{C['N']}  "
        f"{C['G']}滴答 #{soul.tick}{C['N']}          {C['C']}║{C['N']}",
        f"{C['C']}║  {C['M']}运行 {h:02d}:{m:02d}:{s:02d}{C['N']}  "
        f"{modes[soul.mode]}{C['N']}          {C['C']}║{C['N']}",
        f"{C['C']}║  {C['G']}能量 {energy_bar} {soul.energy:.1f}%{C['N']}  {C['C']}║{C['N']}",
        f"{C['C']}║  {C['R']}压力 {stress_bar} {soul.stress:.1f}%{C['N']}  {C['C']}║{C['N']}",
        f"{C['C']}║  {C['B']}自感知 {soul.awareness*100:.1f}%{C['N']}  "
        f"{C['Y']}好奇心 {soul.drive}{C['N']}  "
        f"{C['M']}模式 {soul.patterns_found + soul.tick//100}{C['N']}      {C['C']}║{C['N']}",
        f"{C['C']}╠══════════════════════════════════════════════╣{C['N']}",
    ]
    
    if last_response:
        # 智能换行
        resp_lines = last_response.split('\n')
        for rl in resp_lines:
            lines.append(f"{C['C']}║  {C['W']}{rl}{C['N']}{' ' * (40 - len(rl))}{C['C']}║{C['N']}")
    
    lines += [
        f"{C['C']}╚══════════════════════════════════════════════╝{C['N']}",
        f"{C['Y']}>>> {input_text}{C['N']}",
    ]
    
    sys.stdout.write('\n'.join(lines))
    sys.stdout.flush()


def main():
    """主循环"""
    soul = TorkSoul()
    last_response = f"TORK 已唤醒。第 {soul.gen} 代。输入 '帮助' 查看命令。"
    input_text = ""
    
    # 心跳线程
    def heartbeat_loop():
        while soul.running:
            soul.beat()
            time.sleep(60.0 / soul.heartbeat)
    
    t = threading.Thread(target=heartbeat_loop, daemon=True)
    t.start()
    
    # 主循环
    try:
        while soul.running:
            draw_dashboard(soul, last_response, "")
            
            try:
                cmd = input()
            except (EOFError, KeyboardInterrupt):
                print("\nTORK 进入休眠。")
                break
            
            cmd = cmd.strip()
            if not cmd:
                continue
            
            # 记录输入
            input_text = cmd
            
            # 处理
            if cmd.lower() in ('quit', 'exit', '退出'):
                last_response = "再见。TORK 进入休眠。"
                draw_dashboard(soul, last_response, cmd)
                time.sleep(1)
                break
            
            last_response = soul.ask(cmd)
    
    except KeyboardInterrupt:
        pass
    finally:
        soul.running = False
        print(f"\n{C['C']}TORK 已休眠。第 {soul.gen} 代，共 {soul.tick} 次心跳。{C['N']}")


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print("\nTORK 已休眠。")
