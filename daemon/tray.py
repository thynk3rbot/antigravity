#!/usr/bin/env python3
"""
Magic System Tray — wraps the daemon with a Windows system tray icon.

Usage:
    python daemon/tray.py [--port 8001] [--mqtt-broker localhost:1883]

Requires: pip install pystray Pillow httpx
The daemon runs in a background thread; pystray owns the main thread (Windows requirement).
"""

import threading
import asyncio
import webbrowser
import time
import sys
import os
import argparse

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "src"))

try:
    import pystray
    from pystray import MenuItem as item
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("[Tray] Missing dependencies. Run: pip install pystray Pillow")
    sys.exit(1)

try:
    import httpx
except ImportError:
    httpx = None

from main import MagicDaemon

# ── Global state ──────────────────────────────────────────────────────────────
_daemon: MagicDaemon = None
_loop: asyncio.AbstractEventLoop = None
_tray_icon = None
_status = "starting"  # starting | healthy | degraded | stopped
_peers_online = 0
_args = None

# ── Icon drawing ──────────────────────────────────────────────────────────────

def _make_icon(status: str = "starting") -> Image.Image:
    size = 64
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    fg = {"healthy": "#00ff88", "starting": "#ffaa00", "degraded": "#ffaa00", "stopped": "#ff4444"}.get(status, "#888")

    # Background disc
    d.ellipse([1, 1, 62, 62], fill="#141426", outline="#00d4ff", width=2)

    # Mesh node: center dot + 3 signal arcs
    cx, cy = 32, 32
    d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=fg)
    d.arc([cx - 14, cy - 14, cx + 14, cy + 14], start=200, end=340, fill=fg, width=3)
    d.arc([cx - 22, cy - 22, cx + 22, cy + 22], start=200, end=340, fill=fg, width=2)
    d.arc([cx - 14, cy - 14, cx + 14, cy + 14], start=20,  end=160, fill=fg, width=3)
    d.arc([cx - 22, cy - 22, cx + 22, cy + 22], start=20,  end=160, fill=fg, width=2)

    # Small status dot in bottom-right corner
    dot_color = {"healthy": "#00ff88", "degraded": "#ffaa00", "stopped": "#ff4444", "starting": "#ffaa00"}[status]
    d.ellipse([46, 46, 60, 60], fill=dot_color, outline="#141426", width=2)

    return img


# ── Menu ──────────────────────────────────────────────────────────────────────

def _status_label() -> str:
    labels = {
        "healthy":  f"● Healthy — {_peers_online} device{'s' if _peers_online != 1 else ''} online",
        "starting": "◌ Starting...",
        "degraded": "⚠ Degraded",
        "stopped":  "✕ Stopped",
    }
    return labels.get(_status, _status)


def _open_webapp(icon, _item):
    webbrowser.open("http://localhost:8000")


def _open_api(icon, _item):
    webbrowser.open("http://localhost:8001/docs")


def _restart(icon, _item):
    global _status, _daemon, _loop
    _status = "starting"
    _refresh_tray()
    if _loop and _daemon:
        asyncio.run_coroutine_threadsafe(_daemon.shutdown(), _loop).result(timeout=5)
    _start_daemon_thread()


def _quit(icon, _item):
    global _status
    _status = "stopped"
    if _loop and _daemon:
        try:
            asyncio.run_coroutine_threadsafe(_daemon.shutdown(), _loop).result(timeout=5)
        except Exception:
            pass
    icon.stop()
    sys.exit(0)


def _make_menu():
    return pystray.Menu(
        item("Magic Daemon", None, enabled=False),
        item(_status_label(), None, enabled=False),
        pystray.Menu.SEPARATOR,
        item("Open Webapp  :8000", _open_webapp, default=True),
        item("Open API Docs  :8001", _open_api),
        pystray.Menu.SEPARATOR,
        item("Restart Daemon", _restart),
        pystray.Menu.SEPARATOR,
        item("Quit", _quit),
    )


def _refresh_tray():
    if _tray_icon:
        _tray_icon.icon = _make_icon(_status)
        _tray_icon.title = f"Magic — {_status_label()}"
        _tray_icon.menu = _make_menu()


# ── Daemon thread ─────────────────────────────────────────────────────────────

def _daemon_thread():
    global _daemon, _loop, _status
    _loop = asyncio.new_event_loop()
    asyncio.set_event_loop(_loop)

    port = _args.port if _args else 8001
    broker = _args.mqtt_broker if _args else "localhost:1883"
    _daemon = MagicDaemon(port=port, mqtt_broker=broker)

    try:
        _loop.run_until_complete(_daemon.initialize())
        _status = "healthy"
        _refresh_tray()
        _loop.run_until_complete(_daemon.start())
    except Exception as e:
        _status = "stopped"
        _refresh_tray()
        print(f"[Tray] Daemon error: {e}")


def _start_daemon_thread():
    t = threading.Thread(target=_daemon_thread, daemon=True, name="Magic-Daemon")
    t.start()


# ── Health polling ────────────────────────────────────────────────────────────

def _health_poll_thread():
    global _status, _peers_online
    while True:
        time.sleep(10)
        if _status == "stopped":
            break
        try:
            if httpx:
                r = httpx.get("http://localhost:8001/health", timeout=3)
                if r.status_code == 200:
                    data = r.json()
                    _peers_online = data.get("peers_online", 0)
                    new = "healthy"
                else:
                    new = "degraded"
            else:
                new = _status
        except Exception:
            new = "degraded" if _status == "healthy" else _status

        if new != _status or True:  # always refresh to update peer count
            _status = new
            _refresh_tray()


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    global _tray_icon, _args

    parser = argparse.ArgumentParser(description="Magic Daemon (system tray)")
    parser.add_argument("--port", type=int, default=8001)
    parser.add_argument("--mqtt-broker", type=str, default="localhost:1883")
    _args = parser.parse_args()

    _start_daemon_thread()

    _tray_icon = pystray.Icon(
        "magic-daemon",
        icon=_make_icon("starting"),
        title="Magic — Starting...",
        menu=_make_menu(),
    )

    # Health poll runs in background
    threading.Thread(target=_health_poll_thread, daemon=True, name="Magic-Health").start()

    # pystray.run() MUST be on the main thread on Windows
    _tray_icon.run()


if __name__ == "__main__":
    main()
