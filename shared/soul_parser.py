"""
Soul 解析 — 唯一权威定义，与 engine/soul_access.h 严格同步。
所有 Python 文件必须通过此模块解析 Soul，禁止独立实现。
"""
import struct

SOUL_SIZE = 192
SOUL_ADDR = 0x200000

# ── Offset 定义 (与 soul_access.h 一一对应) ──
OFFSETS = {
    "tick":              (0x00, "<I"),  # uint32
    "last_tsc":          (0x04, "<Q"),  # uint64
    "cur_tsc":           (0x0C, "<Q"),  # uint64
    "elapsed":           (0x14, "<Q"),  # uint64
    "expected":          (0x1C, "<Q"),  # uint64
    "hw_stress":         (0x24, "B"),   # uint8
    "mode":              (0x25, "B"),   # uint8
    "crc":               (0x28, "<I"),  # uint32
    "self_pid":          (0x2C, "<I"),  # uint32
    "drive":             (0x30, "b"),   # int8
    "ppid":              (0x32, "<H"),  # uint16
    "code_insns":        (0x34, "<H"),  # uint16
    "code_mov":          (0x36, "<H"),  # uint16
    "code_arith":        (0x38, "<H"),  # uint16
    "code_ctrl":         (0x3A, "<H"),  # uint16
    "code_other":        (0x3C, "<H"),  # uint16
    "code_mod_success":  (0x3E, "B"),   # uint8
    "code_opt_saved":    (0x3F, "B"),   # uint8
    "code_nop_count":    (0x40, "B"),   # uint8
    "fission_count":     (0x41, "B"),   # uint8
    "child_pid":         (0x42, "<H"),  # uint16
    "fission_tick":      (0x44, "<H"),  # uint16
    "wins":              (0x46, "<H"),  # uint16
    "agreed":            (0x48, "B"),   # uint8
    "sandbox_level":     (0x49, "B"),   # uint8
    "cloud_connected":   (0x4A, "B"),   # uint8
    "cloud_provider":    (0x4B, "B"),   # uint8
    "learn_count":       (0x4C, "<H"),  # uint16
    "mutation_count":    (0x4E, "<H"),  # uint16
    "best_score":        (0x50, "<H"),  # uint16
    "gen_count":         (0x54, "<I"),  # uint32
    "experience_count":  (0x60, "<I"),  # uint32
    "experience_saved":  (0x64, "<I"),  # uint32
    "learning_rate":     (0x68, "<H"),  # uint16
    "curiosity_decay":   (0x6A, "<H"),  # uint16
    "mcts_iterations":   (0x6C, "<H"),  # uint16
    "branch_id":         (0x80, "<I"),  # uint32
    "parent_id":         (0x84, "<I"),  # uint32
    "branch_gen":        (0x88, "<I"),  # uint32
}

# 简化版：只解析常用字段
SIMPLE_FIELDS = [
    "tick", "hw_stress", "mode", "drive", "self_pid", "ppid",
    "code_insns", "code_ctrl", "fission_count", "wins",
    "agreed", "sandbox_level", "cloud_connected", "learn_count",
    "gen_count", "experience_count",
]


def parse_soul(data: bytes, fields: list[str] | None = None) -> dict:
    """Parse soul binary data into a dict. Uses authoritative C layout."""
    if not data or len(data) < 0x58:
        return {}

    result = {}
    parse_fields = fields if fields is not None else SIMPLE_FIELDS

    for name in parse_fields:
        if name not in OFFSETS:
            continue
        offset, fmt = OFFSETS[name]
        if offset + struct.calcsize(fmt) > len(data):
            continue
        result[name] = struct.unpack_from(fmt, data, offset)[0]

    return result


def parse_soul_full(data: bytes) -> dict:
    """Parse all known soul fields."""
    return parse_soul(data, fields=list(OFFSETS.keys()))


def read_soul_from_proc(pid: int) -> dict:
    """Read soul from /proc/PID/mem at SOUL_ADDR."""
    try:
        pid = int(pid)
        if pid <= 0:
            return {}
        with open(f"/proc/{pid}/maps", "r") as f:
            for line in f:
                if "200000" in line[:12]:
                    break
        with open(f"/proc/{pid}/mem", "rb") as f:
            f.seek(SOUL_ADDR)
            data = f.read(SOUL_SIZE)
        if len(data) == SOUL_SIZE:
            return parse_soul_full(data)
    except (OSError, ValueError):
        pass
    return {}
