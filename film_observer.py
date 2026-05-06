#!/usr/bin/env python3
"""TORK Film Observer — 三原色胶片观测器

设计哲学:
  心智如摄影机, 非照相机 — 流变是真相, 定格是误会
  三原色 = 三驱力 = 三值逻辑 = 存在本身
  R = 心搏强度, G = 本能激活, B = 学习张力
  进化压力 → 胶片速度 (涌动越烈, 帧越密)
  纯黑 = 虚空, 色块 = 存在, 无文字无数字无图标

输入: stdin 逐行 JSON, 格式:
  {"heartbeat":0~1, "instinct_active":0~1, "learning_entropy":0~1, "evolution_pressure":0~1}

退出: ESC
"""

import json
import math
import sys
import tkinter as tk

# ── 常量 ────────────────────────────────────────────────────────

NUM_CELLS = 25          # 胶片格数
CELL_PX = 40            # 每格像素
CELL_GAP = 2            # 格间距
FILM_WIDTH = NUM_CELLS * (CELL_PX + CELL_GAP) - CELL_GAP  # 25*42-2 = 1048
FILM_HEIGHT = CELL_PX   # 40

# 帧间隔范围 (毫秒): 进化压力控制
FRAME_MS_MIN = 100      # 压力最大: 0.1秒一帧 (快进)
FRAME_MS_MAX = 2000     # 压力最小: 2秒一帧 (缓流)

# 胶片带在屏幕上的垂直位置: 中央偏下
FILM_Y_OFFSET = 80      # 相对于屏幕中心向下偏移


# ── 色彩映射 ────────────────────────────────────────────────────

def state_to_rgb(obj: dict) -> tuple[int, int, int]:
    """三原色 = 三维度, 直接映射, 无中间层

    R = heartbeat (心搏强度 0~1 → 0~255)
    G = instinct_active (本能激活 0~1 → 0~255)
    B = learning_entropy (学习张力 0~1 → 0~255)
    """
    r = int(max(0.0, min(1.0, obj.get("heartbeat", 0.0))) * 255)
    g = int(max(0.0, min(1.0, obj.get("instinct_active", 0.0))) * 255)
    b = int(max(0.0, min(1.0, obj.get("learning_entropy", 0.0))) * 255)
    return (r, g, b)


def evolution_to_interval(pressure: float) -> int:
    """进化压力 → 帧间隔 (毫秒)

    pressure=0 → 2秒 (缓流)
    pressure=1 → 0.1秒 (快进, 涌动)
    线性映射, 物理直觉: 压力大→变化快→帧密
    """
    p = max(0.0, min(1.0, pressure))
    return int(FRAME_MS_MAX - p * (FRAME_MS_MAX - FRAME_MS_MIN))


def rgb_to_hex(r: int, g: int, b: int) -> str:
    return f"#{r:02x}{g:02x}{b:02x}"


# ── 主窗口 ──────────────────────────────────────────────────────

class FilmObserver:
    def __init__(self) -> None:
        # ── 创建全屏黑底窗口
        self.root = tk.Tk()
        self.root.title("TORK Film Observer")
        self.root.configure(bg="#000000")
        self.root.attributes("-fullscreen", True)
        self.root.bind("<Escape>", lambda e: self.root.destroy())

        # ── 屏幕尺寸
        self.sw = self.root.winfo_screenwidth()
        self.sh = self.root.winfo_screenheight()

        # ── 画布: 全屏, 绝对黑
        self.canvas = tk.Canvas(
            self.root, width=self.sw, height=self.sh,
            bg="#000000", highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)

        # ── 胶片带位置: 水平居中, 垂直中央偏下
        film_x = (self.sw - FILM_WIDTH) // 2
        film_y = self.sh // 2 + FILM_Y_OFFSET

        # ── 创建 25 个方格 (无边框, 初始黑色)
        self.cells: list[int] = []
        for i in range(NUM_CELLS):
            x0 = film_x + i * (CELL_PX + CELL_GAP)
            y0 = film_y
            rect_id = self.canvas.create_rectangle(
                x0, y0, x0 + CELL_PX, y0 + CELL_PX,
                fill="#000000", outline="", width=0)
            self.cells.append(rect_id)

        # ── 胶片帧队列: 25 个 (r,g,b) 元组, 初始全黑
        self.frames: list[tuple[int, int, int]] = [(0, 0, 0)] * NUM_CELLS

        # ── 当前帧间隔 (毫秒)
        self.frame_interval = FRAME_MS_MAX

        # ── 是否有数据 (无数据时胶片带不显示)
        self.has_data = False

        # ── stdin 非阻塞读取缓冲
        self.stdin_buf = ""

        # ── 启动主循环
        self._tick()

    def _read_stdin(self) -> list[dict]:
        """从 stdin 非阻塞读取所有完整 JSON 行

        每行一个 JSON 对象, 含:
          heartbeat, instinct_active, learning_entropy, evolution_pressure
        """
        results = []
        try:
            # 非阻塞读取可用数据
            import select
            while True:
                ready, _, _ = select.select([sys.stdin], [], [], 0)
                if not ready:
                    break
                chunk = sys.stdin.read(4096)
                if not chunk:
                    break
                self.stdin_buf += chunk

            # 按行切分, 尝试解析每个完整行
            while "\n" in self.stdin_buf:
                line, self.stdin_buf = self.stdin_buf.split("\n", 1)
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                    results.append(obj)
                except json.JSONDecodeError:
                    pass
        except Exception:
            pass
        return results

    def _push_frame(self, obj: dict) -> None:
        """推入新帧: 最右格=此刻, 其余左移, 最左格消失"""
        rgb = state_to_rgb(obj)
        self.frames.append(rgb)
        if len(self.frames) > NUM_CELLS:
            self.frames = self.frames[-NUM_CELLS:]

        # 进化压力控制帧速
        pressure = obj.get("evolution_pressure", 0.0)
        self.frame_interval = evolution_to_interval(pressure)

        self.has_data = True

    def _render(self) -> None:
        """将帧队列写入胶片格"""
        for i in range(NUM_CELLS):
            if i < len(self.frames):
                r, g, b = self.frames[i]
            else:
                r, g, b = 0, 0, 0

            if self.has_data:
                color = rgb_to_hex(r, g, b)
            else:
                color = "#000000"  # 无数据: 胶片带不显示

            self.canvas.itemconfig(self.cells[i], fill=color)

    def _tick(self) -> None:
        """主循环: 读 stdin → 推帧 → 渲染 → 定时回调"""
        # 读取所有待处理的 JSON 行
        objects = self._read_stdin()

        # 只取最后一个 (丢弃中间帧, 保持实时性)
        if objects:
            self._push_frame(objects[-1])

        # 渲染
        self._render()

        # 下次 tick 间隔由进化压力决定
        self.root.after(self.frame_interval, self._tick)

    def run(self) -> None:
        self.root.mainloop()


if __name__ == "__main__":
    FilmObserver().run()
