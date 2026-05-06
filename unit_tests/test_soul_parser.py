"""soul_parser 模块单元测试 — 与 shared/soul_parser.py 偏移定义严格同步。"""
import os
import struct
import sys
import unittest
from unittest.mock import mock_open, patch

# 确保 import 路径正确
BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, BASE)

from shared.soul_parser import (
    OFFSETS,
    SIMPLE_FIELDS,
    SOUL_SIZE,
    SOUL_ADDR,
    parse_soul,
    parse_soul_full,
    parse_soul_hex,
    read_soul_from_proc,
)


def _build_soul(**overrides: int | bytes) -> bytes:
    """构造一份 SOUL_SIZE 长度的 bytes，按 OFFSETS 填充零值后覆盖指定字段。"""
    buf = bytearray(SOUL_SIZE)
    for name, value in overrides.items():
        offset, fmt = OFFSETS[name]
        struct.pack_into(fmt, buf, offset, value)
    return bytes(buf)


class TestParseSoulEdgeCases(unittest.TestCase):
    """parse_soul 边界条件。"""

    def test_empty_bytes_returns_empty_dict(self) -> None:
        self.assertEqual(parse_soul(b""), {})

    def test_short_data_returns_empty_dict(self) -> None:
        # 0x57 字节，刚好不足 0x58 阈值
        self.assertEqual(parse_soul(b"\x00" * 0x57), {})


class TestParseSoulFields(unittest.TestCase):
    """parse_soul 正确解析各字段。"""

    def test_tick_field(self) -> None:
        # tick: offset 0x00, uint32
        data = _build_soul(tick=0xDEADBEEF)
        result = parse_soul(data)
        self.assertEqual(result["tick"], 0xDEADBEEF)

    def test_drive_field(self) -> None:
        # drive: offset 0x30, int8 (有符号)
        data = _build_soul(drive=-42)
        result = parse_soul(data)
        self.assertEqual(result["drive"], -42)

    def test_hw_stress_field(self) -> None:
        # hw_stress: offset 0x24, uint8
        data = _build_soul(hw_stress=200)
        result = parse_soul(data)
        self.assertEqual(result["hw_stress"], 200)

    def test_fields_parameter_filters_output(self) -> None:
        data = _build_soul(tick=123, hw_stress=77, drive=-1)
        result = parse_soul(data, fields=["tick", "hw_stress"])
        self.assertIn("tick", result)
        self.assertIn("hw_stress", result)
        self.assertNotIn("drive", result)
        self.assertEqual(result["tick"], 123)
        self.assertEqual(result["hw_stress"], 77)

    def test_fields_parameter_unknown_field_skipped(self) -> None:
        data = _build_soul(tick=42)
        result = parse_soul(data, fields=["tick", "nonexistent"])
        self.assertIn("tick", result)
        self.assertNotIn("nonexistent", result)


class TestParseSoulFull(unittest.TestCase):
    """parse_soul_full 解析全部字段。"""

    def test_all_offsets_parsed(self) -> None:
        # 给每个 OFFSETS 字段写入可辨识的值
        overrides: dict[str, int | bytes] = {}
        for name, (offset, fmt) in OFFSETS.items():
            if fmt.endswith("s"):
                overrides[name] = b"\xAA" * struct.calcsize(fmt)
            elif fmt == "B":
                overrides[name] = 0xFF  # uint8
            elif fmt == "b":
                overrides[name] = -1    # int8
            elif fmt.startswith("<"):
                size = struct.calcsize(fmt)
                overrides[name] = (1 << (size * 8 - 1)) - 1  # 最大正值
            else:
                overrides[name] = 1
        data = _build_soul(**overrides)
        result = parse_soul_full(data)
        # 确认每个 OFFSETS 键都出现在结果中
        for name in OFFSETS:
            self.assertIn(name, result, f"parse_soul_full 缺少字段: {name}")


class TestParseSoulHex(unittest.TestCase):
    """parse_soul_hex 十六进制解析。"""

    def test_hex_string_parsing(self) -> None:
        tick_val = 0x11223344
        data = _build_soul(tick=tick_val)
        hex_str = data.hex()
        result = parse_soul_hex(hex_str)
        self.assertEqual(result["tick"], tick_val)

    def test_0x_prefix_stripped(self) -> None:
        tick_val = 99
        data = _build_soul(tick=tick_val)
        hex_str = "0x" + data.hex()
        result = parse_soul_hex(hex_str)
        self.assertEqual(result["tick"], tick_val)

    def test_0X_prefix_stripped(self) -> None:
        tick_val = 99
        data = _build_soul(tick=tick_val)
        hex_str = "0X" + data.hex()
        result = parse_soul_hex(hex_str)
        self.assertEqual(result["tick"], tick_val)

    def test_invalid_hex_returns_empty_dict(self) -> None:
        self.assertEqual(parse_soul_hex("zzzz"), {})

    def test_none_input_returns_empty_dict(self) -> None:
        self.assertEqual(parse_soul_hex(None), {})

    def test_whitespace_stripped(self) -> None:
        tick_val = 7
        data = _build_soul(tick=tick_val)
        hex_str = "  " + data.hex() + "  "
        result = parse_soul_hex(hex_str)
        self.assertEqual(result["tick"], tick_val)


class TestSoulConstants(unittest.TestCase):
    """常量与 OFFSETS 结构验证。"""

    def test_soul_size_is_208(self) -> None:
        self.assertEqual(SOUL_SIZE, 208)

    def test_offsets_contains_node_id(self) -> None:
        self.assertIn("node_id", OFFSETS)

    def test_offsets_contains_consensus_vector(self) -> None:
        self.assertIn("consensus_vector", OFFSETS)

    def test_node_id_returns_bytes(self) -> None:
        node_id_val = b"\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F\x10"
        data = _build_soul(node_id=node_id_val)
        result = parse_soul(data, fields=["node_id"])
        self.assertIsInstance(result["node_id"], bytes)
        self.assertEqual(result["node_id"], node_id_val)

    def test_consensus_vector_returns_bytes(self) -> None:
        cv_val = b"\xDE" * 16
        data = _build_soul(consensus_vector=cv_val)
        result = parse_soul(data, fields=["consensus_vector"])
        self.assertIsInstance(result["consensus_vector"], bytes)
        self.assertEqual(result["consensus_vector"], cv_val)


class TestParseSoulTruncatedData(unittest.TestCase):
    """数据截断时字段应安全跳过。"""

    def test_field_beyond_data_length_skipped(self) -> None:
        # 只提供 0x58 字节 — node_id (0xA8) 和 consensus_vector (0xB8) 无法读取
        data = _build_soul(tick=42)[:0x58]
        result = parse_soul(data, fields=["tick", "node_id"])
        self.assertEqual(result["tick"], 42)
        self.assertNotIn("node_id", result)


class TestReadSoulFromProc(unittest.TestCase):
    """read_soul_from_proc 的 mock 测试。"""

    def _valid_soul_bytes(self) -> bytes:
        return _build_soul(tick=1234, drive=-5)

    def _maps_with_soul(self) -> str:
        return "00400000-00500000 r-xp 00000000 08:01 1234  /torkd\n200000-r-- 0 0\n"

    def test_success_returns_parsed_soul(self) -> None:
        soul_data = self._valid_soul_bytes()
        maps_content = self._maps_with_soul()

        maps_open = mock_open(read_data=maps_content)
        mem_open = mock_open(read_data=soul_data)

        def _open_side_effect(path: str, mode: str = "r"):
            if path.endswith("/maps"):
                return maps_open.return_value
            if path.endswith("/mem"):
                return mem_open.return_value
            raise FileNotFoundError(path)

        with patch("builtins.open", side_effect=_open_side_effect):
            result = read_soul_from_proc(1000)

        self.assertIsNotNone(result)
        assert result is not None
        self.assertEqual(result["tick"], 1234)
        self.assertEqual(result["drive"], -5)

    def test_maps_no_soul_region_returns_none(self) -> None:
        maps_content = "00400000-00500000 r-xp 00000000 08:01 1234  /torkd\n"
        with patch("builtins.open", mock_open(read_data=maps_content)):
            result = read_soul_from_proc(1000)
        self.assertIsNone(result)

    def test_invalid_pid_returns_none(self) -> None:
        self.assertIsNone(read_soul_from_proc(-1))
        self.assertIsNone(read_soul_from_proc(0))

    def test_proc_not_found_returns_none(self) -> None:
        with patch("builtins.open", side_effect=OSError("no such file")):
            result = read_soul_from_proc(99999)
        self.assertIsNone(result)

    def test_short_read_returns_none(self) -> None:
        """如果 /proc/pid/mem 读取不足 SOUL_SIZE 字节，返回 None。"""
        maps_content = self._maps_with_soul()
        short_data = b"\x00" * (SOUL_SIZE - 1)

        maps_open = mock_open(read_data=maps_content)
        mem_open = mock_open(read_data=short_data)

        def _open_side_effect(path: str, mode: str = "r"):
            if path.endswith("/maps"):
                return maps_open.return_value
            if path.endswith("/mem"):
                return mem_open.return_value
            raise FileNotFoundError(path)

        with patch("builtins.open", side_effect=_open_side_effect):
            result = read_soul_from_proc(1000)
        self.assertIsNone(result)


if __name__ == "__main__":
    unittest.main()
