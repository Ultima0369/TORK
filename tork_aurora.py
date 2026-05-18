#!/usr/bin/env python3
"""TORK Film Observer — 三原色胶片观测器

设计理念:
  三原色 = 三驱力 = 三值逻辑 = 存在本身
  胶片格子 = 心智如摄影机, 不是照相机
  每格 = 一个 tick 的真实状态
  连续播放 = 流变的映射, 不是定格的截图
  纯黑 = 虚空, 存在在虚空中显现

  不需要极光/光团/辉光——那是人类审美的投射
  只需要: 色彩, 格子, 流变
"""

import json
import math
import socket
import time
import tkinter as tk

SOCKET_PATH = "/tmp/torkd.sock"
FILM_COLS = 16
FILM_ROWS = 9
CELL_SIZE = 48
MARGIN = 2
PERF_H = 8
PERF_W = 6
FPS = 12
POLL_MS = 500
HISTORY_LEN = FILM_COLS * FILM_ROWS  # 144 frames


# ── Unix Socket ─────────────────────────────────────────────────

def query_state() -> dict | None:
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(2.0)
        s.connect(SOCKET_PATH)
        s.sendall(b"state\n")
        data = b""
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
        s.close()
        text = data.decode("utf-8", errors="replace").strip()
        if not text or not text.startswith("{"):
            return None
        return json.loads(text)
    except (ConnectionRefusedError, FileNotFoundError, socket.timeout,
            json.JSONDecodeError, OSError):
        return None


# ── 色彩映射 ────────────────────────────────────────────────────

def clamp(v: float, lo: float = 0.0, hi: float = 1.0) -> float:
    return max(lo, min(hi, v))


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def tonemap(r: float, g: float, b: float) -> tuple[float, float, float]:
    luma = 0.2126 * r + 0.7152 * g + 0.0722 * b
    if luma < 0.001:
        return (r, g, b)
    mapped = luma / (1.0 + luma)
    scale = mapped / luma
    return (r * scale, g * scale, b * scale)


def state_to_rgb(state: dict) -> tuple[float, float, float]:
    """将 TORK 状态映射为单一 RGB — 三原色 = 三驱力

    fear → R, desire → G, curiosity → B
    drive 调制整体能量 (不是亮度开关)
    hw_stress 向暖端偏移
    heartbeat 速度调制脉动相位
    """
    hb = state.get("heartbeat", {})
    inst = state.get("instinct", {})
    learn = state.get("learning", {})
    evo = state.get("evolution", {})

    # 三驱力 → 三原色 (核心映射, 不可简化)
    fear = inst.get("fear", 0.0)
    desire = inst.get("desire", 0.0)
    curiosity = inst.get("curiosity", 0.0)

    r = clamp(fear / 1.2)
    g = clamp(desire / 1.2)
    b = clamp(curiosity / 1.2)

    # drive: 能量调制 — 不是开关, 是振幅
    drive = hb.get("drive", 0)
    energy = 0.5 + 0.5 * abs(drive) / 128.0
    r *= energy
    g *= energy
    b *= energy

    # hw_stress: 向暖端偏移 (物理: 压力→温度→红移)
    hw = hb.get("hw_stress", 0)
    warmth = hw / 3.0 * 0.15
    r = clamp(r + warmth * (1.0 - r))
    g = clamp(g + warmth * 0.3 * (1.0 - g))

    # TLN: 认知活性微调色相
    tln_signed = (learn.get("tln_action", 0) + learn.get("tln_modify", 0) +
                  learn.get("tln_explore", 0) + learn.get("tln_energy", 0)) / 4.0
    # +1 → 微暖, -1 → 微冷
    if tln_signed > 0:
        r = clamp(r + tln_signed * 0.05)
        g = clamp(g + tln_signed * 0.03)
    elif tln_signed < 0:
        b = clamp(b - tln_signed * 0.05)

    # 进化: 世代涌动微调饱和度
    gen_count = evo.get("gen_count", 0)
    mut_count = evo.get("mutation_count", 0)
    if gen_count > 0:
        mutation_rate = mut_count / gen_count
        # 变异风暴: 微弱蓝紫脉冲
        tick = hb.get("tick", 0)
        storm = clamp(mutation_rate / 5.0) * 0.08
        pulse = 0.5 + 0.5 * math.sin(tick * 0.3)
        b = clamp(b + storm * pulse)

    return tonemap(r, g, b)


def rgb_to_hex(r: float, g: float, b: float) -> str:
    return f"#{int(r*255):02x}{int(g*255):02x}{int(b*255):02x}"


# ── 胶片渲染 ────────────────────────────────────────────────────

class FilmStrip:
    """胶片条 — 每格一个 tick, 连续播放 = 流变"""

    def __init__(self) -> None:
        self.history: list[tuple[float, float, float]] = []
        self.connected = False
        self.ember = (0.08, 0.02, 0.01)  # 断连余烬: 暗红

    def push(self, state: dict | None) -> None:
        if state is None:
            self.connected = False
            # 断连: 推入余烬
            self.history.append(self.ember)
        else:
            self.connected = True
            self.history.append(state_to_rgb(state))

        # 保持固定长度
        if len(self.history) > HISTORY_LEN:
            self.history = self.history[-HISTORY_LEN:]

    def get_cell_color(self, idx: int) -> tuple[float, float, float]:
        if idx < len(self.history):
            return self.history[idx]
        return (0.0, 0.0, 0.0)  # 虚空 = 纯黑


# ── tkinter 主窗口 ──────────────────────────────────────────────

class FilmApp:
    def __init__(self) -> None:
        self.root = tk.Tk()
        self.root.title("TORK Film Observer")
        self.root.configure(bg="#000000")
        self.root.resizable(True, True)
        self.root.bind("<Escape>", lambda e: self.root.destroy())
        self.root.bind("<F11>", self._toggle_fullscreen)
        self.root.bind("<f>", self._toggle_fullscreen)

        # 画布尺寸
        canvas_w = FILM_COLS * (CELL_SIZE + MARGIN) + MARGIN
        canvas_h = (FILM_ROWS * (CELL_SIZE + MARGIN) + MARGIN +
                    PERF_H * 2 + MARGIN * 2)

        self.canvas = tk.Canvas(self.root, width=canvas_w, height=canvas_h,
                                bg="#000000", highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)

        # 创建胶片格子
        self.cells: list[int] = []  # canvas rect ids
        self.perfs_top: list[int] = []
        self.perfs_bot: list[int] = []

        # 顶部齿孔
        for col in range(FILM_COLS):
            x = MARGIN + col * (CELL_SIZE + MARGIN) + (CELL_SIZE - PERF_W) // 2
            y = MARGIN
            pid = self.canvas.create_rectangle(
                x, y, x + PERF_W, y + PERF_H,
                fill="#0a0a0a", outline="#111111", width=1)
            self.perfs_top.append(pid)

        # 胶片格子
        for row in range(FILM_ROWS):
            for col in range(FILM_COLS):
                x = MARGIN + col * (CELL_SIZE + MARGIN)
                y = MARGIN + PERF_H + MARGIN + row * (CELL_SIZE + MARGIN)
                rid = self.canvas.create_rectangle(
                    x, y, x + CELL_SIZE, y + CELL_SIZE,
                    fill="#000000", outline="#080808", width=1)
                self.cells.append(rid)

        # 底部齿孔
        for col in range(FILM_COLS):
            x = MARGIN + col * (CELL_SIZE + MARGIN) + (CELL_SIZE - PERF_W) // 2
            y = MARGIN + PERF_H + MARGIN + FILM_ROWS * (CELL_SIZE + MARGIN)
            pid = self.canvas.create_rectangle(
                x, y, x + PERF_W, y + PERF_H,
                fill="#0a0a0a", outline="#111111", width=1)
            self.perfs_bot.append(pid)

        # 当前格高亮框
        self.highlight: int | None = None

        # 状态栏
        self.status_var = tk.StringVar(value="connecting...")
        self.status_label = tk.Label(
            self.root, textvariable=self.status_var,
            fg="#333333", bg="#000000",
            font=("Courier", 9), anchor=tk.W)
        self.status_label.place(x=8, rely=1.0, y=-18, relwidth=1.0)

        self.film = FilmStrip()
        self.fullscreen = False
        self.last_poll = 0.0
        self.frame_idx = 0

    def _toggle_fullscreen(self, event: tk.Event | None = None) -> None:
        self.fullscreen = not self.fullscreen
        self.root.attributes("-fullscreen", self.fullscreen)

    def _poll_state(self) -> None:
        state = query_state()
        self.film.push(state)

        if state:
            hb = state.get("heartbeat", {})
            inst = state.get("instinct", {})
            self.status_var.set(
                f"tick={hb.get('tick', 0)} drive={hb.get('drive', 0):+d} "
                f"fear={inst.get('fear', 0):.2f} desire={inst.get('desire', 0):.2f} "
                f"curiosity={inst.get('curiosity', 0):.2f}  "
                f"[ESC=quit F11=fullscreen]")
        else:
            self.status_var.set("disconnected  [ESC=quit]")

    def _render_frame(self) -> None:
        now = time.monotonic()

        # 轮询
        if now - self.last_poll > POLL_MS / 1000.0:
            self._poll_state()
            self.last_poll = now

        # 更新所有格子颜色
        for idx in range(FILM_COLS * FILM_ROWS):
            r, g, b = self.film.get_cell_color(idx)
            color = rgb_to_hex(r, g, b)
            self.canvas.itemconfig(self.cells[idx], fill=color)

        # 当前写入位置高亮
        write_pos = len(self.film.history) - 1
        if write_pos >= 0 and write_pos < len(self.cells):
            if self.highlight is not None:
                self.canvas.itemconfig(self.cells[self.highlight], outline="#080808")
            self.canvas.itemconfig(self.cells[write_pos], outline="#222222")
            self.highlight = write_pos

        self.frame_idx += 1
        self.root.after(1000 // FPS, self._render_frame)

    def run(self) -> None:
        self._render_frame()
        self.root.mainloop()


if __name__ == "__main__":
    app = FilmApp()
    app.run()
