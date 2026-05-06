"""Async bridge to torkd Unix socket."""
from __future__ import annotations

import asyncio
import concurrent.futures
import logging
import os
import socket

TORKD_SOCK: str = os.environ.get("TORKD_SOCK", "/tmp/torkd.sock")
MAX_RESPONSE_SIZE: int = 8192
_MAX_CONCURRENT: int = 4

_TORKD_POOL: concurrent.futures.ThreadPoolExecutor = concurrent.futures.ThreadPoolExecutor(
    max_workers=_MAX_CONCURRENT, thread_name_prefix="torkd"
)
_TORKD_SEMAPHORE: asyncio.Semaphore = asyncio.Semaphore(_MAX_CONCURRENT)
logger: logging.Logger = logging.getLogger(__name__)


def _sync_query(cmd: str, timeout: float = 10.0) -> str | None:
    sock: socket.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        sock.settimeout(timeout)
        sock.connect(TORKD_SOCK)
        sock.sendall(f"{cmd}\n".encode())
        buf: bytearray = bytearray()
        while len(buf) < MAX_RESPONSE_SIZE:
            chunk: bytes = sock.recv(4096)
            if not chunk:
                break
            buf.extend(chunk)
            # torkd sends a final newline then closes — read until EOF
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


async def torkd_query(cmd: str, timeout: float = 10.0) -> str | None:
    async with _TORKD_SEMAPHORE:
        loop: asyncio.AbstractEventLoop = asyncio.get_running_loop()
        return await loop.run_in_executor(_TORKD_POOL, _sync_query, cmd, timeout)
