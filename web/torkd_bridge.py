"""Async bridge to torkd Unix socket."""
from __future__ import annotations

import asyncio
import concurrent.futures
import logging
import os
import socket

TORKD_SOCK = os.environ.get("TORKD_SOCK", "/tmp/torkd.sock")
MAX_RESPONSE_SIZE = 1_048_576
_MAX_CONCURRENT = 4

_TORKD_POOL = concurrent.futures.ThreadPoolExecutor(
    max_workers=_MAX_CONCURRENT, thread_name_prefix="torkd"
)
_TORKD_SEMAPHORE = asyncio.Semaphore(_MAX_CONCURRENT)
logger = logging.getLogger(__name__)


def _sync_query(cmd: str, timeout: float = 2.0) -> str | None:
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        sock.settimeout(timeout)
        sock.connect(TORKD_SOCK)
        sock.sendall(f"{cmd}\n".encode())
        buf = bytearray()
        while len(buf) < MAX_RESPONSE_SIZE:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buf.extend(chunk)
            if buf.find(b"\n") != -1:
                buf = buf[:buf.find(b"\n")]
                break
        return buf.decode(errors="replace").strip() or None
    except ConnectionRefusedError:
        logger.warning("torkd not running at %s", TORKD_SOCK)
        return None
    except socket.timeout:
        logger.warning("torkd query timed out: %s", cmd)
        return None
    except FileNotFoundError:
        logger.error("torkd socket not found: %s", TORKD_SOCK)
        return None
    except Exception:
        logger.exception("torkd query failed: %s", cmd)
        return None
    finally:
        sock.close()


async def torkd_query(cmd: str, timeout: float = 2.0) -> str | None:
    async with _TORKD_SEMAPHORE:
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(_TORKD_POOL, _sync_query, cmd, timeout)