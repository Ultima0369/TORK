#!/usr/bin/env python3
"""TORK AI Web Dashboard — aiohttp + WebSocket"""
from __future__ import annotations

import asyncio
import json
import logging
import os
import sys
import webbrowser

from aiohttp import web

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, BASE)
sys.path.insert(0, os.path.join(BASE, "api"))  # noqa: E402

from web.torkd_bridge import torkd_query
from shared.soul_parser import read_soul_from_proc, parse_soul_full, parse_soul_hex

STATIC_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")
EVOLUTION_LOG = os.path.join(BASE, "persist", "evolution.json")
INBOX = os.path.join(BASE, "inbox.md")
API_CONFIG = os.path.join(BASE, "api", "api_config.json")
SAFE_BASE = os.path.normpath(BASE) + os.sep

logger = logging.getLogger(__name__)

# Instinct derivation constants
FEAR_HW_FACTOR = 30
FEAR_MODE_BONUS = 10
DESIRE_BASE = 70
DESIRE_HW_FACTOR = 15
DESIRE_MODE_BONUS = 10
CURIOSITY_BASE = 55
CURIOSITY_HW_FACTOR = 8
CURIOSITY_MODE_BONUS = 15

MODE_LABELS = ["explore", "seek", "cautious"]
SANDBOX_LABELS = ["none", "read", "safe", "normal", "full"]


def _safe_path(path: str) -> str | None:
    full = os.path.normpath(os.path.join(BASE, path))
    if full != os.path.normpath(BASE) and not full.startswith(SAFE_BASE):
        return None
    return full


# ── Helpers ────────────────────────────────────────────────────


def _read_json(path: str, default=None):
    if not os.path.exists(path):
        return default if default is not None else {}
    try:
        with open(path) as f:
            return json.load(f)
    except Exception:
        logger.warning("Failed to read JSON: %s", path, exc_info=True)
        return default if default is not None else {}


async def _find_tork_pid():
    loop = asyncio.get_running_loop()
    try:
        r = await asyncio.create_subprocess_exec(
            "pgrep", "-x", "tork_core",
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        stdout, _ = await asyncio.wait_for(r.communicate(), timeout=1)
        if stdout.strip():
            return int(stdout.strip().split(b"\n")[0])
        r = await asyncio.create_subprocess_exec(
            "pgrep", "-x", "tork_engine",
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        stdout, _ = await asyncio.wait_for(r.communicate(), timeout=1)
        if stdout.strip():
            return int(stdout.strip().split(b"\n")[0])
    except Exception:
        logger.debug("pgrep failed", exc_info=True)
    return None


def _derive_instincts(soul):
    if not soul:
        return {"fear": 0, "desire": 50, "curiosity": 50}
    hs = soul.get("hw_stress", 0)
    mode = soul.get("mode", 0)
    fear = min(100, max(0, hs * FEAR_HW_FACTOR + (FEAR_MODE_BONUS if mode == 2 else 0)))
    desire = min(100, max(0, DESIRE_BASE - hs * DESIRE_HW_FACTOR + (DESIRE_MODE_BONUS if mode == 1 else 0)))
    curiosity = min(100, max(0, CURIOSITY_BASE - hs * CURIOSITY_HW_FACTOR + (CURIOSITY_MODE_BONUS if mode == 0 else 0)))
    return {"fear": fear, "desire": desire, "curiosity": curiosity}


def _evolution_stats():
    log = _read_json(EVOLUTION_LOG)
    if isinstance(log, dict) and "mutations" in log:
        mutations = log["mutations"]
        total = len(mutations)
        successes = sum(1 for m in mutations if m.get("result") == "success")
        return {
            "generation": log.get("generation", 0),
            "total_mutations": total,
            "successes": successes,
            "success_rate": round(successes / max(1, total) * 100),
            "recent": mutations[-10:],
        }
    elif isinstance(log, list) and log:
        last = log[-1]
        return {
            "generation": last.get("generation", 0),
            "total_mutations": len(log),
            "successes": sum(1 for e in log if e.get("status") == "success"),
            "success_rate": 0,
            "recent": log[-10:],
        }
    return {"generation": 0, "total_mutations": 0, "successes": 0, "success_rate": 0, "recent": []}


def _api_config():
    return _read_json(API_CONFIG, {"model": "astron-code-latest", "api_key": "", "base_url": "https://maas-coding-api.cn-huabei-1.xf-yun.com/v2", "configured": False})


# ── Shared evolution runner ──────────────────────────────────


async def _run_evolution() -> dict:
    proc = await asyncio.create_subprocess_exec(
        sys.executable, os.path.join(BASE, "cloud", "evolution.py"), "--once",
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    try:
        stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout=120)
    except asyncio.TimeoutError:
        proc.kill()
        await proc.wait()
        return {"output": "Evolution timed out (120s)", "code": -1}
    return {
        "output": stdout.decode(errors="replace")[-500:] if stdout else "",
        "error": stderr.decode(errors="replace")[-200:] if stderr else "",
        "code": proc.returncode,
    }


# ── REST handlers ──────────────────────────────────────────────


async def index(request: web.Request) -> web.FileResponse:
    return web.FileResponse(os.path.join(STATIC_DIR, "dashboard.html"))


async def api_exec(request: web.Request) -> web.Response:
    body = await request.json()
    cmd = body.get("command", "").strip()
    if not cmd:
        return web.json_response({"error": "empty command"}, status=400)
    result = await torkd_query(f"exec:{cmd}")
    return web.json_response({"output": result or "(no response)"})


async def api_evolve(request: web.Request) -> web.Response:
    try:
        result = await _run_evolution()
        return web.json_response(result)
    except Exception as e:
        logger.exception("Evolution failed")
        return web.json_response({"error": str(e)}, status=500)


async def api_evolution_log(request: web.Request) -> web.Response:
    return web.json_response(_evolution_stats())


async def api_config_get(request: web.Request) -> web.Response:
    cfg = _api_config()
    cfg["api_key"] = bool(cfg.get("api_key"))
    return web.json_response(cfg)


async def api_config_set(request: web.Request) -> web.Response:
    body = await request.json()
    allowed = {"model", "api_key", "base_url", "temperature", "max_tokens", "timeout", "configured"}
    filtered = {k: v for k, v in body.items() if k in allowed}
    os.makedirs(os.path.dirname(API_CONFIG), exist_ok=True)
    with open(API_CONFIG, "w") as f:
        json.dump(filtered, f, indent=2)
    return web.json_response({"ok": True})


async def api_file_read(request: web.Request) -> web.Response:
    path = request.match_info.get("path", "")
    full = _safe_path(path)
    if full is None:
        return web.json_response({"error": "forbidden"}, status=403)
    if not os.path.isfile(full):
        return web.json_response({"error": "not found"}, status=404)
    try:
        loop = asyncio.get_running_loop()
        content = await loop.run_in_executor(None, _read_file_sync, full)
        return web.json_response({"content": content, "path": path})
    except Exception as e:
        logger.exception("File read failed: %s", full)
        return web.json_response({"error": str(e)}, status=500)


def _read_file_sync(path: str) -> str:
    with open(path) as f:
        return f.read()


async def api_file_write(request: web.Request) -> web.Response:
    body = await request.json()
    path = body.get("path", "")
    content = body.get("content", "")
    full = _safe_path(path)
    if full is None:
        return web.json_response({"error": "forbidden"}, status=403)
    try:
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, _write_file_sync, full, content)
        return web.json_response({"ok": True})
    except Exception as e:
        logger.exception("File write failed: %s", full)
        return web.json_response({"error": str(e)}, status=500)


def _write_file_sync(path: str, content: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(content)


async def api_inbox(request: web.Request) -> web.Response:
    if os.path.exists(INBOX):
        loop = asyncio.get_running_loop()
        content = await loop.run_in_executor(None, _read_file_sync, INBOX)
        return web.json_response({"content": content, "exists": True})
    return web.json_response({"content": "", "exists": False})


async def api_dir(request: web.Request) -> web.Response:
    dir_path = request.match_info.get("path", "")
    full = _safe_path(dir_path)
    if full is None:
        return web.json_response({"error": "forbidden"}, status=403)
    if not os.path.isdir(full):
        return web.json_response({"error": "not a directory"}, status=400)
    try:
        entries = []
        for name in sorted(os.listdir(full)):
            p = os.path.join(full, name)
            is_dir = os.path.isdir(p)
            entries.append({"name": name, "dir": is_dir,
                            "path": os.path.join(dir_path, name) if dir_path else name})
        return web.json_response({"entries": entries, "path": dir_path})
    except Exception as e:
        return web.json_response({"error": str(e)}, status=500)


# ── WebSocket ──────────────────────────────────────────────────

WS_CLIENTS: set[web.WebSocketResponse] = set()


async def ws_handler(request: web.Request) -> web.WebSocketResponse:
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    WS_CLIENTS.add(ws)
    try:
        async for msg in ws:
            if msg.type == web.WSMsgType.TEXT:
                try:
                    data = json.loads(msg.data)
                except json.JSONDecodeError:
                    await ws.send_json({"type": "error", "data": {"message": "invalid JSON"}})
                    continue
                await _handle_ws(ws, data)
    finally:
        WS_CLIENTS.discard(ws)
    return ws


async def _handle_ws(ws: web.WebSocketResponse, data: dict) -> None:
    t = data.get("type", "")
    if t == "chat":
        msg = data.get("data", {}).get("message", "")
        history = data.get("data", {}).get("history")
        reply = await _chat_reply(msg, history)
        await ws.send_json({"type": "chat_reply", "data": {"role": "tork", "content": reply}})
    elif t == "exec":
        cmd = data.get("data", {}).get("command", "")
        result = await torkd_query(f"exec:{cmd}")
        await ws.send_json({"type": "exec_result", "data": {"output": result or "(no response)"}})
    elif t == "evolve":
        try:
            result = await _run_evolution()
            await ws.send_json({
                "type": "evolution_result",
                "data": {"output": result.get("output", ""), "code": result.get("code", -1)},
            })
        except Exception as e:
            logger.exception("WS evolve failed")
            await ws.send_json({"type": "evolution_result", "data": {"output": f"Error: {e}", "code": -1}})


_CHAT_API = None


def _get_chat_api():
    global _CHAT_API
    if _CHAT_API is not None:
        return _CHAT_API
    try:
        from tork_api import TorkAPI
        _CHAT_API = TorkAPI()
    except Exception:
        logger.debug("TorkAPI not available", exc_info=True)
    return _CHAT_API


async def _chat_reply(message: str, history: list | None = None) -> str:
    api = _get_chat_api()
    if api and api.api_key:
        try:
            if history:
                api.conversation = list(history)
                return api.ask(message, temperature=0.7)
            return api.ask_simple(message, temperature=0.7)
        except Exception:
            logger.warning("Chat API call failed", exc_info=True)
    replies = [
        "I'm processing that.", "Noted — analyzing the pattern.",
        "Interesting. Let me compare approaches.", "Understood. Adjusting my model.",
        "That aligns with what I've observed.",
    ]
    return replies[len(message) % len(replies)]


# ── Background poll ────────────────────────────────────────────

_last_push: str = ""


async def poll_loop(app: web.Application) -> None:
    while True:
        try:
            await _push_update()
        except Exception:
            logger.warning("poll_loop error", exc_info=True)
        await asyncio.sleep(1)


async def _push_update() -> None:
    global _last_push
    if not WS_CLIENTS:
        return
    pid = await _find_tork_pid()
    soul = None
    if pid:
        soul = read_soul_from_proc(pid)
        if not soul:
            raw = await torkd_query("soul")
            if raw:
                try:
                    soul = parse_soul_hex(raw)
                except Exception:
                    logger.debug("Soul hex parse failed", exc_info=True)
    instincts = _derive_instincts(soul)
    evo = _evolution_stats()
    mentor_raw = await torkd_query("mentor")
    mentor = None
    if mentor_raw:
        try:
            mentor = json.loads(mentor_raw)
        except json.JSONDecodeError:
            logger.debug("Mentor parse failed")
    msg = {
        "type": "update",
        "data": {
            "soul": soul,
            "instincts": instincts,
            "engine_running": pid is not None,
            "pid": pid,
            "evolution": evo,
            "mentor": mentor,
        },
    }
    payload = json.dumps(msg)
    if payload == _last_push:
        return
    _last_push = payload
    dead: set[web.WebSocketResponse] = set()
    for ws in list(WS_CLIENTS):
        try:
            await ws.send_str(payload)
        except Exception:
            dead.add(ws)
    WS_CLIENTS.difference_update(dead)


# ── App ────────────────────────────────────────────────────────


def create_app() -> web.Application:
    app = web.Application()
    app.router.add_get("/", index)
    app.router.add_get("/ws", ws_handler)
    app.router.add_post("/api/exec", api_exec)
    app.router.add_post("/api/evolve", api_evolve)
    app.router.add_get("/api/evolution-log", api_evolution_log)
    app.router.add_get("/api/config", api_config_get)
    app.router.add_post("/api/config", api_config_set)
    app.router.add_get("/api/file/{path:.*}", api_file_read)
    app.router.add_post("/api/file", api_file_write)
    app.router.add_get("/api/inbox", api_inbox)
    app.router.add_get("/api/dir", api_dir)
    app.router.add_get("/api/dir/{path:.*}", api_dir)
    app.on_startup.append(_start_poll)
    app.on_cleanup.append(_stop_poll)
    return app


async def _start_poll(app: web.Application) -> None:
    app["poll_task"] = asyncio.create_task(poll_loop(app))
    if not app.get("no_open"):
        port = app["port"]
        asyncio.get_running_loop().call_later(1, webbrowser.open, f"http://localhost:{port}")


async def _stop_poll(app: web.Application) -> None:
    app["poll_task"].cancel()
    try:
        await app["poll_task"]
    except asyncio.CancelledError:
        pass


def main() -> None:
    import argparse
    parser = argparse.ArgumentParser(description="TORK AI Web Dashboard")
    parser.add_argument("--port", "-p", type=int, default=8420)
    parser.add_argument("--no-open", action="store_true")
    args = parser.parse_args()

    app = create_app()
    app["no_open"] = args.no_open
    app["port"] = args.port
    web.run_app(app, host="127.0.0.1", port=args.port, print=None)


if __name__ == "__main__":
    main()