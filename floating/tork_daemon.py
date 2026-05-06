from __future__ import annotations

#!/usr/bin/env python3
"""
TORK 后台守护进程
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
管理 TORK 引擎生命周期 + 仪表盘进程
"""

import subprocess
import sys
import os
import time
import signal
import json
import atexit
from types import FrameType

# ─── 路径 ────────────────────────────────────────────
BASE_DIR: str = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE_PATH: str = os.path.join(BASE_DIR, "build", "tork_engine")
DASHBOARD_PATH: str = os.path.join(BASE_DIR, "floating", "tork_dashboard.py")
CLOUD_PATH: str = os.path.join(BASE_DIR, "cloud", "cloud_protocol.py")
PERSIST_DIR: str = os.path.join(BASE_DIR, "persist")
PIDFILE: str = os.path.join(PERSIST_DIR, "daemon.pid")


class TORKDaemon:
    def __init__(self) -> None:
        self.engine_proc: subprocess.Popen[str] | None = None
        self.dashboard_proc: subprocess.Popen[str] | None = None
        self.running: bool = True
        self._cleaned_up: bool = False

        # 信号处理
        signal.signal(signal.SIGTERM, self._cleanup)
        signal.signal(signal.SIGINT, self._cleanup)

    def start_engine(self) -> bool:
        """启动 TORK 引擎"""
        if not os.path.exists(ENGINE_PATH):
            print(f"⚠️ 引擎未编译: {ENGINE_PATH}")
            print(f"   运行 make 编译")
            return False

        try:
            self.engine_proc = subprocess.Popen(
                [ENGINE_PATH],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=BASE_DIR
            )
            print(f"✅ 引擎已启动 (PID: {self.engine_proc.pid})")
            return True
        except Exception as e:
            print(f"❌ 引擎启动失败: {e}")
            return False

    def stop_engine(self) -> None:
        """停止 TORK 引擎"""
        if self.engine_proc and self.engine_proc.poll() is None:
            self.engine_proc.terminate()
            try:
                self.engine_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.engine_proc.kill()
            print("⏹️  引擎已停止")

    def start_dashboard(self) -> bool:
        """启动仪表盘 (非阻塞)"""
        try:
            self.dashboard_proc = subprocess.Popen(
                [sys.executable, DASHBOARD_PATH],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=BASE_DIR
            )
            print(f"✅ 仪表盘已启动 (PID: {self.dashboard_proc.pid})")
            return True
        except Exception as e:
            print(f"❌ 仪表盘启动失败: {e}")
            return False

    def stop_dashboard(self) -> None:
        """停止仪表盘"""
        if self.dashboard_proc and self.dashboard_proc.poll() is None:
            self.dashboard_proc.terminate()
            try:
                self.dashboard_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.dashboard_proc.kill()
            print("⏹️  仪表盘已停止")

    def get_status(self) -> dict[str, int | bool]:
        """获取守护进程状态"""
        return {
            "engine_running": self.engine_proc is not None and self.engine_proc.poll() is None,
            "engine_pid": self.engine_proc.pid if self.engine_proc else 0,
            "dashboard_running": self.dashboard_proc is not None and self.dashboard_proc.poll() is None,
            "dashboard_pid": self.dashboard_proc.pid if self.dashboard_proc else 0,
        }

    def _cleanup(self, signum: int | None = None, frame: FrameType | None = None) -> None:
        """清理退出"""
        if self._cleaned_up:
            return
        self._cleaned_up = True
        self.stop_dashboard()
        self.stop_engine()
        self.running = False
        # 清理 PID 文件
        try:
            if os.path.exists(PIDFILE):
                os.remove(PIDFILE)
        except OSError:
            pass
        sys.exit(0)

    def run(self, mode: str = "all") -> None:
        """运行守护进程"""
        os.makedirs(PERSIST_DIR, exist_ok=True)

        # 保存 PID
        with open(PIDFILE, "w") as f:
            f.write(str(os.getpid()))

        atexit.register(self._cleanup)

        if mode in ("engine", "all"):
            self.start_engine()

        if mode in ("dashboard", "all"):
            self.start_dashboard()

        if mode == "all":
            print(f"\nTORK 守护进程运行中")
            print(f"   引擎 PID: {self.engine_proc.pid if self.engine_proc else '—'}")
            print(f"   仪表盘 PID: {self.dashboard_proc.pid if self.dashboard_proc else '—'}")
            print(f"   按 Ctrl+C 停止所有进程\n")

            # 保持运行，监控子进程
            try:
                while self.running:
                    # 检查子进程状态
                    if self.engine_proc and self.engine_proc.poll() is not None:
                        print("⚠️ 引擎意外退出，重启中…")
                        self.start_engine()
                    if self.dashboard_proc and self.dashboard_proc.poll() is not None:
                        print("⚠️ 仪表盘意外退出，重启中…")
                        self.start_dashboard()
                    time.sleep(5)
            except KeyboardInterrupt:
                self._cleanup()


def main() -> None:
    import argparse
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description="TORK 守护进程")
    parser.add_argument("action", nargs="?", default="all",
                        choices=["all", "engine", "dashboard", "status", "stop"],
                        help="操作类型")
    args: argparse.Namespace = parser.parse_args()

    daemon: TORKDaemon = TORKDaemon()

    if args.action == "stop":
        if os.path.exists(PIDFILE):
            with open(PIDFILE) as f:
                pid: int = int(f.read().strip())
            try:
                os.kill(pid, signal.SIGTERM)
                print(f"⏹️  已发送停止信号到 PID {pid}")
            except ProcessLookupError:
                print("⚠️ 守护进程未运行")
                os.remove(PIDFILE)
        else:
            print("⚠️ 守护进程未运行")
        return

    if args.action == "status":
        status: dict[str, int | bool] = daemon.get_status()
        print(f"引擎: {'✅ 运行中' if status['engine_running'] else '⏹️ 已停止'} (PID: {status['engine_pid']})")
        print(f"仪表盘: {'✅ 运行中' if status['dashboard_running'] else '⏹️ 已停止'} (PID: {status['dashboard_pid']})")
        return

    daemon.run(args.action)


if __name__ == "__main__":
    main()
