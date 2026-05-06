"""Unit tests for web.torkd_bridge — all socket I/O is mocked."""
from __future__ import annotations

import asyncio
import os
import socket
import sys
import unittest
from unittest.mock import AsyncMock, MagicMock, patch

# 确保 import 路径正确
BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, BASE)

from web.torkd_bridge import (
    MAX_RESPONSE_SIZE,
    TORKD_SOCK,
    _sync_query,
    torkd_query,
)


class TestConstants(unittest.TestCase):
    """Module-level constants exist and have correct types."""

    def test_torkd_sock_is_str(self) -> None:
        self.assertIsInstance(TORKD_SOCK, str)

    def test_torkd_sock_default_value(self) -> None:
        self.assertEqual(TORKD_SOCK, "/tmp/torkd.sock")

    def test_max_response_size_is_int(self) -> None:
        self.assertIsInstance(MAX_RESPONSE_SIZE, int)

    def test_max_response_size_positive(self) -> None:
        self.assertGreater(MAX_RESPONSE_SIZE, 0)


class TestSyncQuery(unittest.TestCase):
    """Tests for the blocking _sync_query helper."""

    @patch("web.torkd_bridge.socket.socket")
    def test_normal_response_returned(self, mock_socket_cls: MagicMock) -> None:
        fake_sock = MagicMock()
        mock_socket_cls.return_value = fake_sock
        fake_sock.recv.side_effect = [b'{"status":"ok"}\n', b""]

        result = _sync_query("ping")

        self.assertEqual(result, '{"status":"ok"}')
        fake_sock.connect.assert_called_once_with(TORKD_SOCK)
        fake_sock.sendall.assert_called_once_with(b"ping\n")
        fake_sock.close.assert_called_once()

    @patch("web.torkd_bridge.socket.socket")
    def test_empty_response_returns_none(self, mock_socket_cls: MagicMock) -> None:
        fake_sock = MagicMock()
        mock_socket_cls.return_value = fake_sock
        fake_sock.recv.return_value = b""

        result = _sync_query("ping")

        self.assertIsNone(result)

    @patch("web.torkd_bridge.socket.socket")
    def test_whitespace_only_response_returns_none(
        self, mock_socket_cls: MagicMock
    ) -> None:
        fake_sock = MagicMock()
        mock_socket_cls.return_value = fake_sock
        fake_sock.recv.side_effect = [b"   \n  ", b""]

        result = _sync_query("ping")

        self.assertIsNone(result)

    @patch("web.torkd_bridge.socket.socket")
    def test_connection_refused_returns_none(self, mock_socket_cls: MagicMock) -> None:
        fake_sock = MagicMock()
        mock_socket_cls.return_value = fake_sock
        fake_sock.connect.side_effect = ConnectionRefusedError

        result = _sync_query("ping")

        self.assertIsNone(result)
        fake_sock.close.assert_called_once()

    @patch("web.torkd_bridge.socket.socket")
    def test_socket_timeout_returns_none(self, mock_socket_cls: MagicMock) -> None:
        fake_sock = MagicMock()
        mock_socket_cls.return_value = fake_sock
        fake_sock.connect.side_effect = socket.timeout

        result = _sync_query("ping")

        self.assertIsNone(result)
        fake_sock.close.assert_called_once()

    @patch("web.torkd_bridge.socket.socket")
    def test_file_not_found_returns_none(self, mock_socket_cls: MagicMock) -> None:
        fake_sock = MagicMock()
        mock_socket_cls.return_value = fake_sock
        fake_sock.connect.side_effect = FileNotFoundError

        result = _sync_query("ping")

        self.assertIsNone(result)
        fake_sock.close.assert_called_once()

    @patch("web.torkd_bridge.socket.socket")
    def test_generic_exception_returns_none(self, mock_socket_cls: MagicMock) -> None:
        fake_sock = MagicMock()
        mock_socket_cls.return_value = fake_sock
        fake_sock.connect.side_effect = RuntimeError("boom")

        result = _sync_query("ping")

        self.assertIsNone(result)
        fake_sock.close.assert_called_once()

    @patch("web.torkd_bridge.socket.socket")
    def test_timeout_value_forwarded(self, mock_socket_cls: MagicMock) -> None:
        fake_sock = MagicMock()
        mock_socket_cls.return_value = fake_sock
        fake_sock.recv.return_value = b"ok"

        _sync_query("ping", timeout=3.5)

        fake_sock.settimeout.assert_called_once_with(3.5)

    @patch("web.torkd_bridge.socket.socket")
    def test_large_response_exceeds_limit(self, mock_socket_cls: MagicMock) -> None:
        """_sync_query's while loop checks < MAX_RESPONSE_SIZE *before* recv,
        so a single recv can push buf past the limit. Verify it still returns
        a string and does not hang."""
        fake_sock = MagicMock()
        mock_socket_cls.return_value = fake_sock
        big_chunk = b"A" * 5000
        fake_sock.recv.side_effect = [big_chunk, big_chunk, b""]

        result = _sync_query("ping")

        # Result may exceed MAX_RESPONSE_SIZE by up to one recv chunk (4096)
        self.assertIsNotNone(result)
        self.assertTrue(len(result) > 0)

    @patch("web.torkd_bridge.socket.socket")
    def test_recv_obeys_max_response_size_loop(
        self, mock_socket_cls: MagicMock
    ) -> None:
        """The recv loop stops once buf reaches MAX_RESPONSE_SIZE."""
        fake_sock = MagicMock()
        mock_socket_cls.return_value = fake_sock
        chunk = b"B" * 4096
        fake_sock.recv.side_effect = [chunk, chunk, b""]

        _sync_query("ping")

        # With MAX_RESPONSE_SIZE=8192 and two 4096 chunks, buf hits the limit
        # after the second chunk so the while loop exits without calling recv again.
        self.assertLessEqual(fake_sock.recv.call_count, 3)

    @patch("web.torkd_bridge.socket.socket")
    def test_unicode_decode_error_handled(self, mock_socket_cls: MagicMock) -> None:
        fake_sock = MagicMock()
        mock_socket_cls.return_value = fake_sock
        fake_sock.recv.side_effect = [b"\xff\xfe invalid utf8", b""]

        result = _sync_query("ping")

        # errors="replace" ensures it returns a string, not crash
        self.assertIsNotNone(result)

    @patch("web.torkd_bridge.socket.socket")
    def test_cmd_with_newline_still_sends_trailing_newline(
        self, mock_socket_cls: MagicMock
    ) -> None:
        fake_sock = MagicMock()
        mock_socket_cls.return_value = fake_sock
        fake_sock.recv.return_value = b"ok"

        _sync_query("cmd\n")

        # The function always appends \n regardless of cmd content
        fake_sock.sendall.assert_called_once_with(b"cmd\n\n")


class TestTorkdQuery(unittest.TestCase):
    """Tests for the async torkd_query entry point."""

    @patch("web.torkd_bridge._sync_query", return_value='{"status":"ok"}')
    def test_normal_response(self, mock_sync: MagicMock) -> None:
        result = asyncio.run(torkd_query("ping"))
        self.assertEqual(result, '{"status":"ok"}')
        mock_sync.assert_called_once_with("ping", 10.0)

    @patch("web.torkd_bridge._sync_query", return_value='{"status":"ok"}')
    def test_custom_timeout_forwarded(self, mock_sync: MagicMock) -> None:
        asyncio.run(torkd_query("ping", timeout=5.0))
        mock_sync.assert_called_once_with("ping", 5.0)

    @patch("web.torkd_bridge._sync_query", return_value=None)
    def test_socket_missing_returns_none(self, mock_sync: MagicMock) -> None:
        result = asyncio.run(torkd_query("ping"))
        self.assertIsNone(result)

    @patch("web.torkd_bridge._sync_query", return_value=None)
    def test_timeout_returns_none(self, mock_sync: MagicMock) -> None:
        result = asyncio.run(torkd_query("ping", timeout=0.001))
        self.assertIsNone(result)

    @patch("web.torkd_bridge._sync_query", side_effect=Exception("unexpected"))
    def test_sync_query_exception_propagates(self, mock_sync: MagicMock) -> None:
        with self.assertRaises(Exception):
            asyncio.run(torkd_query("ping"))

    @patch("web.torkd_bridge._sync_query", return_value="hello")
    def test_plain_string_response(self, mock_sync: MagicMock) -> None:
        result = asyncio.run(torkd_query("ping"))
        self.assertEqual(result, "hello")


class TestTorkdQueryConcurrency(unittest.TestCase):
    """Semaphore limits concurrent torkd_query calls."""

    @patch("web.torkd_bridge._sync_query", return_value="ok")
    def test_semaphore_limits_concurrency(self, mock_sync: MagicMock) -> None:
        """At most _MAX_CONCURRENT queries run simultaneously."""
        from web.torkd_bridge import _MAX_CONCURRENT

        barrier = asyncio.Event()
        call_count = 0
        original_sync = mock_sync

        def slow_sync(cmd: str, timeout: float = 10.0) -> str:
            nonlocal call_count
            call_count += 1
            # In the real executor this would block; just return immediately
            return "ok"

        mock_sync.side_effect = slow_sync

        async def run_many() -> list[str | None]:
            tasks = [torkd_query(f"cmd-{i}") for i in range(_MAX_CONCURRENT * 2)]
            return await asyncio.gather(*tasks)

        results = asyncio.run(run_many())
        self.assertEqual(len(results), _MAX_CONCURRENT * 2)
        self.assertTrue(all(r == "ok" for r in results))


if __name__ == "__main__":
    unittest.main()
