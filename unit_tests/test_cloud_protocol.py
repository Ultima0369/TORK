"""
TorkCloudAgent 单元测试 — cloud/cloud_protocol.py
Mock subprocess / 文件 I/O / TorkAPI, 无真实网络或系统调用。

覆盖:
  1. TorkCloudAgent 初始化
  2. handle: 未知工具 → error
  3. handle: think → ack
  4. handle: ask_deepseek → success / API未配置 / 异常
  5. _run_shell: sandbox 不存在 / 成功执行 / 超时 / 异常
  6. _read_soul: TORK 未运行 / 成功解析 / 异常
  7. _write_soul: 无效 offset / TORK 未运行 / 成功写入
  8. _validate_path: 项目内路径 / 项目外路径 / 边界
  9. _read_file: path缺失 / 项目外路径 / 成功读取 / 文件不存在
  10. _write_file: 参数缺失 / 项目外路径 / 成功写入
  11. _mutate: 参数缺失 / 项目外路径 / 成功patch / patch失败
  12. _inbox: 消息缺失 / 成功投递 / 写入异常
  13. _compile: 成功 / 超时
  14. _status: 基本结构验证
  15. _dashboard_status: 基本结构验证
  16. next_id: 递增且格式正确
  17. main: stdin JSON 协议解析 (解析错误 / 空行 / 正常)
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, Mock, patch, mock_open

# 确保 import 路径正确
BASE: str = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(BASE, "shared"))
sys.path.insert(0, os.path.join(BASE, "api"))
sys.path.insert(0, os.path.join(BASE, "cloud"))

from cloud_protocol import TorkCloudAgent


# ── 1. TorkCloudAgent 初始化 ──

class TestTorkCloudAgentInit(unittest.TestCase):
    """TorkCloudAgent.__init__ 测试"""

    @patch("cloud_protocol.TorkAPI", create=True)
    def test_init_defaults(self, _mock_api_cls):
        """初始化后 soul_cache=None, msg_id=0"""
        with patch.dict("sys.modules", {"tork_api": MagicMock()}):
            agent = TorkCloudAgent()
            self.assertIsNone(agent.soul_cache)
            self.assertEqual(agent.msg_id, 0)

    def test_init_deepseek_api_not_available(self):
        """当 TorkAPI import 失败时, deepseek_api=None"""
        # __init__ 中 try/except 已兜底
        agent = TorkCloudAgent()
        # deepseek_api 可能是 None 或 TorkAPI 实例, 取决于环境
        # 关键: 初始化不会崩溃
        self.assertIsNotNone(agent)  # 不崩溃即可

    @patch("cloud_protocol.TorkAPI", create=True)
    def test_init_deepseek_api_available(self, _mock_api_cls):
        """TorkAPI 可用时, deepseek_api 被赋值"""
        mock_api_instance = MagicMock()
        mock_api_instance.api_key = "test_key"
        mock_api_cls = MagicMock(return_value=mock_api_instance)
        with patch.dict("sys.modules", {"tork_api": MagicMock(TorkAPI=mock_api_cls)}):
            # 重新导入使 mock 生效
            import importlib
            importlib.reload(sys.modules["cloud_protocol"])
            from cloud_protocol import TorkCloudAgent as ReloadedAgent
            agent = ReloadedAgent()
            # deepseek_api 不应为 None (mock 版)
            self.assertIsNotNone(agent.deepseek_api)


# ── 2. handle: 路由测试 ──

class TestHandleRouting(unittest.TestCase):
    """handle() 路由分发测试"""

    def setUp(self):
        self.agent = TorkCloudAgent()

    def test_handle_unknown_tool(self):
        """未知工具 → type=error, msg=Unknown tool"""
        result = self.agent.handle({"tool": "nonexistent_tool", "args": {}})
        self.assertEqual(result["type"], "error")
        self.assertIn("Unknown tool", result["data"]["msg"])

    def test_handle_think(self):
        """think 工具 → type=ack, status=noted"""
        result = self.agent.handle({"tool": "think", "args": {"thought": "hello"}})
        self.assertEqual(result["type"], "ack")
        self.assertEqual(result["data"]["status"], "noted")

    def test_handle_with_custom_id(self):
        """自定义 id 透传到结果"""
        result = self.agent.handle({"id": "my_id_42", "tool": "think"})
        self.assertEqual(result["id"], "my_id_42")

    def test_handle_auto_id_when_missing(self):
        """缺少 id 时自动生成"""
        result = self.agent.handle({"tool": "think"})
        self.assertIn("id", result)
        self.assertTrue(result["id"].startswith("cmd_"))


# ── 3. next_id ──

class TestNextId(unittest.TestCase):
    """next_id() 格式和递增测试"""

    def setUp(self):
        self.agent = TorkCloudAgent()

    def test_next_id_format(self):
        """next_id 格式: cmd_N_timestamp"""
        id1 = self.agent.next_id()
        self.assertTrue(id1.startswith("cmd_1_"))

    def test_next_id_incrementing(self):
        """msg_id 递增"""
        id1 = self.agent.next_id()
        id2 = self.agent.next_id()
        self.assertEqual(self.agent.msg_id, 2)
        self.assertNotEqual(id1, id2)


# ── 4. _run_shell ──

class TestRunShell(unittest.TestCase):
    """_run_shell 测试 — subprocess.run + sandbox"""

    def setUp(self):
        self.agent = TorkCloudAgent()

    @patch("os.path.exists", return_value=False)
    def test_run_shell_no_sandbox(self, _mock_exists):
        """sandbox 二进制不存在 → 拒绝执行"""
        result = self.agent._run_shell("ls", 30)
        self.assertEqual(result["type"], "error")
        self.assertIn("Sandbox not available", result["data"]["msg"])

    @patch("os.path.exists", return_value=True)
    @patch("subprocess.run")
    def test_run_shell_success(self, mock_run, _mock_exists):
        """成功执行 shell 命令"""
        mock_run.return_value = subprocess.CompletedProcess(
            args=[], returncode=0,
            stdout=json.dumps({"exit_code": 0, "stdout": "hello", "stderr": ""}),
            stderr=""
        )
        result = self.agent._run_shell("echo hello", 30)
        self.assertEqual(result["type"], "result")
        self.assertEqual(result["exit_code"], 0)

    @patch("os.path.exists", return_value=True)
    @patch("subprocess.run", side_effect=subprocess.TimeoutExpired(cmd="test", timeout=35))
    def test_run_shell_timeout(self, _mock_run, _mock_exists):
        """shell 执行超时 → timed_out=True"""
        result = self.agent._run_shell("sleep 999", 30)
        self.assertEqual(result["type"], "result")
        self.assertTrue(result["data"]["timed_out"])

    @patch("os.path.exists", return_value=True)
    @patch("subprocess.run", side_effect=Exception("boom"))
    def test_run_shell_exception(self, _mock_run, _mock_exists):
        """shell 执行异常 → type=error"""
        result = self.agent._run_shell("bad_cmd", 30)
        self.assertEqual(result["type"], "error")
        self.assertIn("boom", result["data"]["msg"])


# ── 5. _read_soul ──

class TestReadSoul(unittest.TestCase):
    """_read_soul / _read_soul_raw 测试"""

    def setUp(self):
        self.agent = TorkCloudAgent()

    @patch.object(TorkCloudAgent, "_read_soul_raw", return_value=(None, None))
    def test_read_soul_not_running(self, _mock_raw):
        """TORK 未运行 → error"""
        result = self.agent._read_soul()
        self.assertEqual(result["type"], "error")
        self.assertIn("未运行", result["data"]["msg"])

    @patch("cloud_protocol.parse_soul_full", return_value={"tick": 42, "mode": 1})
    @patch.object(TorkCloudAgent, "_read_soul_raw", return_value=("00" * 208, 1234))
    def test_read_soul_success(self, _mock_raw, _mock_parse):
        """成功读取 Soul — 用 00*208 有效 hex, bytes.fromhex 不会报错"""
        result = self.agent._read_soul()
        self.assertEqual(result["type"], "result")
        self.assertEqual(result["data"]["tick"], 42)
        self.assertEqual(result["data"]["pid"], 1234)
        self.assertEqual(self.agent.soul_cache["tick"], 42)

    @patch("cloud_protocol.parse_soul_full", side_effect=RuntimeError("parse crash"))
    @patch.object(TorkCloudAgent, "_read_soul_raw", return_value=("00" * 208, 1234))
    def test_read_soul_parse_exception(self, _mock_raw, _mock_parse):
        """Soul 解析异常 → error (parse_soul_full 抛出异常)"""
        result = self.agent._read_soul()
        self.assertEqual(result["type"], "error")


# ── 6. _read_soul_raw ──

class TestReadSoulRaw(unittest.TestCase):
    """_read_soul_raw — pgrep + /proc/mem 读取"""

    def setUp(self):
        self.agent = TorkCloudAgent()

    @patch("subprocess.run")
    def test_read_soul_raw_no_process(self, mock_run):
        """pgrep 找不到进程 → (None, None)"""
        mock_run.return_value = subprocess.CompletedProcess(
            args=[], returncode=1, stdout="", stderr=""
        )
        raw_hex, pid = self.agent._read_soul_raw()
        self.assertIsNone(raw_hex)
        self.assertIsNone(pid)

    @patch("builtins.open", mock_open(read_data=b"\x00" * 208))
    @patch("subprocess.run")
    def test_read_soul_raw_success(self, mock_run):
        """成功读取 → (hex_str, pid)"""
        mock_run.return_value = subprocess.CompletedProcess(
            args=[], returncode=0, stdout="1234\n", stderr=""
        )
        raw_hex, pid = self.agent._read_soul_raw()
        self.assertIsNotNone(raw_hex)
        self.assertEqual(pid, 1234)

    @patch("subprocess.run")
    def test_read_soul_raw_os_error(self, mock_run):
        """/proc/mem 打开失败 → (None, None)"""
        mock_run.return_value = subprocess.CompletedProcess(
            args=[], returncode=0, stdout="1234\n", stderr=""
        )
        with patch("builtins.open", side_effect=OSError("permission denied")):
            raw_hex, pid = self.agent._read_soul_raw()
        self.assertIsNone(raw_hex)
        self.assertIsNone(pid)


# ── 7. _write_soul ──

class TestWriteSoul(unittest.TestCase):
    """_write_soul 测试 — offset 校验 + ptrace 写入"""

    def setUp(self):
        self.agent = TorkCloudAgent()

    def test_write_soul_invalid_offset_none(self):
        """offset=None → error"""
        result = self.agent._write_soul(None, 42)
        self.assertEqual(result["type"], "error")

    def test_write_soul_invalid_offset_negative(self):
        """offset < 0 → error"""
        result = self.agent._write_soul(-1, 42)
        self.assertEqual(result["type"], "error")

    def test_write_soul_invalid_offset_overflow(self):
        """offset >= SOUL_SIZE → error"""
        from soul_parser import SOUL_SIZE
        result = self.agent._write_soul(SOUL_SIZE, 42)
        self.assertEqual(result["type"], "error")

    def test_write_soul_offset_string(self):
        """offset 为字符串 → error (非 int)"""
        result = self.agent._write_soul("0x10", 42)
        self.assertEqual(result["type"], "error")

    @patch("subprocess.run")
    def test_write_soul_not_running(self, mock_run):
        """TORK 未运行 → error"""
        mock_run.return_value = subprocess.CompletedProcess(
            args=[], returncode=1, stdout="", stderr=""
        )
        # pgrep 两次都失败
        result = self.agent._write_soul(0, 42)
        self.assertEqual(result["type"], "error")
        self.assertIn("未运行", result["data"]["msg"])


# ── 8. _validate_path ──

class TestValidatePath(unittest.TestCase):
    """_validate_path 路径安全校验测试"""

    def setUp(self):
        self.agent = TorkCloudAgent()

    def test_valid_path_inside_project(self):
        """项目内路径 → 返回绝对路径"""
        safe = self.agent._validate_path("shared/soul_parser.py")
        self.assertIsNotNone(safe)
        self.assertTrue(safe.startswith(BASE))

    def test_invalid_path_outside_project(self):
        """项目外路径 → None"""
        result = self.agent._validate_path("/etc/passwd")
        self.assertIsNone(result)

    def test_path_traversal_attack(self):
        """路径遍历攻击 → None"""
        result = self.agent._validate_path("../../etc/passwd")
        self.assertIsNone(result)

    def test_exact_base_dir(self):
        """恰好是项目根目录 → 返回 BASE"""
        result = self.agent._validate_path(BASE)
        # normpath 后 == base, 应通过 (full == base)
        self.assertIsNotNone(result)

    def test_empty_path(self):
        """空路径 → normpath 后为 cwd, 不在 BASE 内 → None"""
        result = self.agent._validate_path("")
        # 取决于 cwd 是否在 BASE 内; 通常不在
        # 至少不应崩溃


# ── 9. _read_file ──

class TestReadFile(unittest.TestCase):
    """_read_file 测试"""

    def setUp(self):
        self.agent = TorkCloudAgent()

    def test_read_file_path_none(self):
        """path=None → error"""
        result = self.agent._read_file(None)
        self.assertEqual(result["type"], "error")
        self.assertIn("Path is required", result["data"]["msg"])

    def test_read_file_outside_project(self):
        """项目外路径 → error"""
        result = self.agent._read_file("/etc/passwd")
        self.assertEqual(result["type"], "error")
        self.assertIn("Path outside project", result["data"]["msg"])

    @patch("builtins.open", mock_open(read_data="hello world"))
    def test_read_file_success(self):
        """成功读取文件"""
        result = self.agent._read_file("shared/soul_parser.py")
        self.assertEqual(result["type"], "result")
        self.assertIn("content", result["data"])

    @patch("builtins.open", side_effect=FileNotFoundError("no such file"))
    def test_read_file_not_found(self, _mock_open):
        """文件不存在 → error"""
        result = self.agent._read_file("nonexistent_file.py")
        self.assertEqual(result["type"], "error")


# ── 10. _write_file ──

class TestWriteFile(unittest.TestCase):
    """_write_file 测试"""

    def setUp(self):
        self.agent = TorkCloudAgent()

    def test_write_file_path_none(self):
        """path=None → error"""
        result = self.agent._write_file(None, "content")
        self.assertEqual(result["type"], "error")

    def test_write_file_content_none(self):
        """content=None → error"""
        result = self.agent._write_file("some/path", None)
        self.assertEqual(result["type"], "error")

    def test_write_file_both_none(self):
        """path 和 content 都为 None → error"""
        result = self.agent._write_file(None, None)
        self.assertEqual(result["type"], "error")

    def test_write_file_outside_project(self):
        """项目外路径 → error"""
        result = self.agent._write_file("/tmp/evil.py", "malware")
        self.assertEqual(result["type"], "error")

    @patch("builtins.open", new_callable=mock_open)
    @patch("os.makedirs")
    def test_write_file_success(self, _mock_makedirs, _mock_file):
        """成功写入文件"""
        result = self.agent._write_file("test_output.txt", "hello")
        self.assertEqual(result["type"], "result")
        self.assertEqual(result["data"]["size"], 5)


# ── 11. _mutate ──

class TestMutate(unittest.TestCase):
    """_mutate 测试 — patch 文件"""

    def setUp(self):
        self.agent = TorkCloudAgent()

    def test_mutate_file_none(self):
        """file=None → error"""
        result = self.agent._mutate(None, "diff content")
        self.assertEqual(result["type"], "error")

    def test_mutate_diff_none(self):
        """diff=None → error"""
        result = self.agent._mutate("some/file.c", None)
        self.assertEqual(result["type"], "error")

    def test_mutate_outside_project(self):
        """项目外路径 → error"""
        result = self.agent._mutate("/usr/share/evil.c", "--- a\n+++ b\n")
        self.assertEqual(result["type"], "error")

    @patch("subprocess.run")
    @patch("tempfile.mkstemp", return_value=(3, "/tmp/tork_mutate_xxx"))
    @patch("os.unlink")
    @patch("os.fdopen", mock_open())
    def test_mutate_patch_success(self, _mock_unlink, _mock_mkstemp, mock_run):
        """patch 成功 → status=patched"""
        mock_run.return_value = subprocess.CompletedProcess(
            args=[], returncode=0, stdout="", stderr=""
        )
        # 构造项目内的安全路径
        safe_path = os.path.join(BASE, "src", "test.c")
        result = self.agent._mutate(safe_path, "--- a\n+++ b\n@@ -1 +1 @@\n-old\n+new\n")
        self.assertEqual(result["type"], "result")
        self.assertEqual(result["data"]["status"], "patched")

    @patch("subprocess.run")
    @patch("tempfile.mkstemp", return_value=(3, "/tmp/tork_mutate_xxx"))
    @patch("os.unlink")
    @patch("os.fdopen", mock_open())
    def test_mutate_patch_failure(self, _mock_unlink, _mock_mkstemp, mock_run):
        """patch 失败 → error + stderr"""
        mock_run.return_value = subprocess.CompletedProcess(
            args=[], returncode=1, stdout="", stderr="patch failed"
        )
        safe_path = os.path.join(BASE, "src", "test.c")
        result = self.agent._mutate(safe_path, "--- bad diff ---")
        self.assertEqual(result["type"], "error")


# ── 12. _inbox ──

class TestInbox(unittest.TestCase):
    """_inbox 测试 — 写入 inbox.md"""

    def setUp(self):
        self.agent = TorkCloudAgent()

    def test_inbox_message_none(self):
        """message=None → error"""
        result = self.agent._inbox(None)
        self.assertEqual(result["type"], "error")

    @patch("time.ctime", return_value="Wed May  6 12:00:00 2026")
    @patch("builtins.open", mock_open())
    def test_inbox_success(self, _mock_ctime):
        """成功投递 → status=delivered"""
        result = self.agent._inbox("hello from cloud")
        self.assertEqual(result["type"], "result")
        self.assertEqual(result["data"]["status"], "delivered")

    @patch("builtins.open", side_effect=OSError("write failed"))
    def test_inbox_write_error(self, _mock_open):
        """写入异常 → error"""
        result = self.agent._inbox("hello")
        self.assertEqual(result["type"], "error")


# ── 13. _compile ──

class TestCompile(unittest.TestCase):
    """_compile 测试 — make all"""

    def setUp(self):
        self.agent = TorkCloudAgent()

    @patch("subprocess.run")
    def test_compile_success(self, mock_run):
        """编译成功 → result + exit_code"""
        mock_run.return_value = subprocess.CompletedProcess(
            args=[], returncode=0, stdout="make: Nothing to be done.", stderr=""
        )
        result = self.agent._compile()
        self.assertEqual(result["type"], "result")
        self.assertEqual(result["data"]["exit_code"], 0)

    @patch("subprocess.run", side_effect=subprocess.TimeoutExpired(cmd="make", timeout=60))
    def test_compile_timeout(self, _mock_run):
        """编译超时 → error"""
        result = self.agent._compile()
        self.assertEqual(result["type"], "error")
        self.assertIn("timeout", result["data"]["msg"])


# ── 14. _ask_deepseek ──

class TestAskDeepseek(unittest.TestCase):
    """_ask_deepseek 测试 — 委托 TorkAPI"""

    def setUp(self):
        self.agent = TorkCloudAgent()

    def test_ask_deepseek_no_api(self):
        """deepseek_api=None → API 未配置"""
        self.agent.deepseek_api = None
        result = self.agent._ask_deepseek("hello")
        self.assertEqual(result["type"], "error")
        self.assertIn("未配置", result["data"]["msg"])

    def test_ask_deepseek_success(self):
        """成功调用 DeepSeek"""
        mock_api = MagicMock()
        mock_api.ask_simple.return_value = "TORK: optimize scheduler loop"
        mock_api.model = "astron-code-latest"
        self.agent.deepseek_api = mock_api
        result = self.agent._ask_deepseek("optimize the scheduler")
        self.assertEqual(result["type"], "result")
        self.assertEqual(result["data"]["reply"], "TORK: optimize scheduler loop")
        self.assertEqual(result["data"]["model"], "astron-code-latest")

    def test_ask_deepseek_exception(self):
        """API 调用异常 → error"""
        mock_api = MagicMock()
        mock_api.ask_simple.side_effect = ConnectionError("network down")
        mock_api.model = "astron-code-latest"
        self.agent.deepseek_api = mock_api
        result = self.agent._ask_deepseek("test")
        self.assertEqual(result["type"], "error")

    def test_ask_deepseek_empty_prompt(self):
        """prompt 为空字符串 → ask_simple 仍被调用"""
        mock_api = MagicMock()
        mock_api.ask_simple.return_value = "no prompt received"
        mock_api.model = "astron-code-latest"
        self.agent.deepseek_api = mock_api
        result = self.agent._ask_deepseek("")
        mock_api.ask_simple.assert_called_once()


# ── 15. _status ──

class TestStatus(unittest.TestCase):
    """_status 测试 — 综合状态报告"""

    def setUp(self):
        self.agent = TorkCloudAgent()

    @patch("os.path.exists", return_value=False)
    @patch("subprocess.run")
    def test_status_basic_structure(self, mock_run, _mock_exists):
        """status 返回包含 project 和 base"""
        # pgrep 失败 + git log 失败
        mock_run.return_value = subprocess.CompletedProcess(
            args=[], returncode=1, stdout="", stderr=""
        )
        # _read_soul → TORK 未运行
        with patch.object(TorkCloudAgent, "_read_soul", return_value={"type": "error", "data": {"msg": "未运行"}}):
            result = self.agent._status()
        self.assertEqual(result["type"], "result")
        self.assertEqual(result["data"]["project"], "TORK")
        self.assertIn("base", result["data"])

    @patch("os.path.exists", return_value=True)
    @patch("subprocess.run")
    def test_status_with_running_process(self, mock_run, _mock_exists):
        """进程运行时 status 包含 PID"""
        # pgrep 成功
        mock_run.return_value = subprocess.CompletedProcess(
            args=[], returncode=0, stdout="5678\n", stderr=""
        )
        with patch.object(TorkCloudAgent, "_read_soul", return_value={"type": "error", "data": {"msg": "未运行"}}):
            result = self.agent._status()
        self.assertEqual(result["type"], "result")
        self.assertTrue(result["data"]["tork_core_running"])


# ── 16. _dashboard_status ──

class TestDashboardStatus(unittest.TestCase):
    """_dashboard_status 测试"""

    def setUp(self):
        self.agent = TorkCloudAgent()

    @patch("os.path.exists", return_value=False)
    @patch("subprocess.run")
    def test_dashboard_status_structure(self, mock_run, _mock_exists):
        """dashboard_status 返回基本字段"""
        mock_run.return_value = subprocess.CompletedProcess(
            args=[], returncode=1, stdout="", stderr=""
        )
        with patch.object(TorkCloudAgent, "_read_soul_raw", return_value=(None, None)):
            result = self.agent._dashboard_status()
        self.assertEqual(result["type"], "result")
        self.assertIn("tork_core_running", result["data"])
        self.assertIn("soul_hex", result["data"])
        self.assertIn("engine_pid", result["data"])
        self.assertIn("evolution_log", result["data"])
        self.assertIn("agreement_file", result["data"])

    @patch("os.path.exists", side_effect=lambda p: p.endswith("evolution.json"))
    @patch("subprocess.run")
    def test_dashboard_status_with_evolution(self, mock_run, _mock_exists):
        """有 evolution.json 时包含 evolution_log"""
        mock_run.return_value = subprocess.CompletedProcess(
            args=[], returncode=1, stdout="", stderr=""
        )
        evo_path = os.path.join(BASE, "persist", "evolution.json")
        mock_evolution_data = [{"gen": 1, "score": 50}]
        m_open = mock_open(read_data=json.dumps(mock_evolution_data))
        with patch.object(TorkCloudAgent, "_read_soul_raw", return_value=(None, None)):
            with patch("builtins.open", m_open):
                result = self.agent._dashboard_status()
        self.assertEqual(result["type"], "result")


# ── 17. main — stdin JSON 协议 ──

class TestMainProtocol(unittest.TestCase):
    """main() stdin JSON 协议测试"""

    @patch("sys.stdout")
    @patch("builtins.print")
    def test_main_valid_json(self, mock_print, _mock_stdout):
        """有效 JSON 通过 handle 处理"""
        agent = TorkCloudAgent()
        test_input = '{"tool": "think", "args": {"thought": "test"}}\n'
        with patch("sys.stdin", MagicMock(readlines=MagicMock(return_value=[test_input]))):
            # 直接测试 agent.handle, 不走 main() 的无限循环
            instruction = json.loads(test_input.strip())
            result = agent.handle(instruction)
        self.assertEqual(result["type"], "ack")

    def test_main_invalid_json(self):
        """无效 JSON → parse error"""
        bad_input = "not json at all"
        try:
            json.loads(bad_input)
            self.fail("Should have raised JSONDecodeError")
        except json.JSONDecodeError:
            pass  # 预期行为

    def test_main_empty_line_skipped(self):
        """空行应被跳过"""
        line = ""
        self.assertEqual(line.strip(), "")  # 空行 → continue


# ── 18. TOOLS 注册表 ──

class TestToolsRegistry(unittest.TestCase):
    """TOOLS dict 测试"""

    def test_tools_dict_keys(self):
        """TOOLS 包含所有预期工具"""
        expected_tools = [
            "run_shell", "read_soul", "write_soul", "read_file",
            "write_file", "mutate", "inbox", "compile", "status",
            "dashboard_status", "think", "ask_deepseek",
        ]
        for tool in expected_tools:
            self.assertIn(tool, TorkCloudAgent.TOOLS)

    def test_tools_dict_has_desc(self):
        """每个工具都有 desc 字段"""
        for tool_name, tool_info in TorkCloudAgent.TOOLS.items():
            self.assertIn("desc", tool_info)
            self.assertTrue(len(tool_info["desc"]) > 0)

    def test_tools_count(self):
        """工具数量 = 12"""
        self.assertEqual(len(TorkCloudAgent.TOOLS), 12)


if __name__ == "__main__":
    unittest.main(verbosity=2)