"""
tray.py — LoRaLink Daemon System Tray Icon

Runs in the background on Windows. Shows daemon + webapp health.
Left-click  → Open Fleet Admin in browser
Right-click → Menu (Open UI, Open API, Restart, Quit)

Usage:
    python -m tools.daemon.tray
    python tools/daemon/tray.py
"""

import threading
import time
import webbrowser
import subprocess
import sys
import urllib.request
import urllib.error
from pathlib import Path

try:
    import pystray
    from PIL import Image, ImageDraw
    TRAY_AVAILABLE = True
except ImportError:
    TRAY_AVAILABLE = False

DAEMON_URL  = "http://localhost:8001"
WEBAPP_URL  = "http://localhost:8000"
POLL_INTERVAL = 5  # seconds

# Icon dimensions
ICON_SIZE = 64

# Status dot colors
COLOR_GREEN  = (0, 212, 100)
COLOR_YELLOW = (255, 180, 0)
COLOR_RED    = (220, 50, 50)
COLOR_BLACK  = (0, 0, 0)

# Paths to look for the brand icon
_HERE = Path(__file__).parent
ICON_PATHS = [
    _HERE.parent / "webapp" / "static" / "media" / "loralink_icon.png",
    _HERE.parent / "webapp" / "static" / "media" / "logo.png",
]


def _load_base_icon() -> "Image.Image":
    """Load the brand icon, falling back to a simple generated icon."""
    for path in ICON_PATHS:
        if path.exists():
            img = Image.open(path).convert("RGBA")
            img = img.resize((ICON_SIZE, ICON_SIZE), Image.LANCZOS)
            return img
    # Fallback: dark square with cyan border
    img = Image.new("RGBA", (ICON_SIZE, ICON_SIZE), (15, 20, 30, 255))
    draw = ImageDraw.Draw(img)
    draw.rectangle([1, 1, ICON_SIZE - 2, ICON_SIZE - 2], outline=(0, 180, 220), width=2)
    return img


def _make_icon(daemon_ok: bool, webapp_ok: bool) -> "Image.Image":
    """Overlay health status dots onto the brand icon."""
    img = _load_base_icon().copy()
    draw = ImageDraw.Draw(img)

    dot_r = 9

    # Daemon dot — top-right corner
    dot_color = COLOR_GREEN if daemon_ok else COLOR_RED
    x0, y0 = ICON_SIZE - dot_r * 2 - 1, 1
    draw.ellipse([x0, y0, x0 + dot_r * 2, y0 + dot_r * 2], fill=COLOR_BLACK)
    draw.ellipse([x0 + 2, y0 + 2, x0 + dot_r * 2 - 2, y0 + dot_r * 2 - 2], fill=dot_color)

    # Webapp dot — bottom-right corner
    webapp_color = COLOR_GREEN if webapp_ok else COLOR_YELLOW
    x1, y1 = ICON_SIZE - dot_r * 2 - 1, ICON_SIZE - dot_r * 2 - 1
    draw.ellipse([x1, y1, x1 + dot_r * 2, y1 + dot_r * 2], fill=COLOR_BLACK)
    draw.ellipse([x1 + 2, y1 + 2, x1 + dot_r * 2 - 2, y1 + dot_r * 2 - 2], fill=webapp_color)

    return img


def _check_health(url: str) -> bool:
    try:
        req = urllib.request.urlopen(
            urllib.request.Request(url + "/health"),
            timeout=2
        )
        return req.status == 200
    except Exception:
        return False


def _check_webapp(url: str) -> bool:
    try:
        req = urllib.request.urlopen(url, timeout=2)
        return req.status == 200
    except Exception:
        return False


class DaemonTray:
    def __init__(self):
        self._icon = None
        self._daemon_proc = None
        self._webapp_proc = None
        self._running = True
        self._daemon_ok = False
        self._webapp_ok = False

    def _title(self) -> str:
        d = "Daemon ✓" if self._daemon_ok else "Daemon ✗"
        w = "UI ✓" if self._webapp_ok else "UI ✗"
        return f"LoRaLink Fleet — {d}  {w}"

    def _open_ui(self, icon=None, item=None):
        webbrowser.open(WEBAPP_URL)

    def _open_api(self, icon=None, item=None):
        webbrowser.open(DAEMON_URL + "/docs")

    def _restart_daemon(self, icon=None, item=None):
        """Kill and restart the daemon process."""
        if self._daemon_proc:
            self._daemon_proc.terminate()
        root = Path(__file__).parent.parent.parent
        self._daemon_proc = subprocess.Popen(
            [sys.executable, "-m", "tools.daemon.daemon"],
            cwd=str(root),
        )

    def _quit(self, icon=None, item=None):
        self._running = False
        if self._icon:
            self._icon.stop()

    def _poll_loop(self):
        """Background thread: poll health and update icon."""
        while self._running:
            self._daemon_ok = _check_health(DAEMON_URL)
            self._webapp_ok = _check_webapp(WEBAPP_URL)
            if self._icon:
                self._icon.icon  = _make_icon(self._daemon_ok, self._webapp_ok)
                self._icon.title = self._title()
            time.sleep(POLL_INTERVAL)

    def run(self):
        if not TRAY_AVAILABLE:
            print("ERROR: pystray and Pillow are required.")
            print("Run: pip install pystray Pillow")
            sys.exit(1)

        # Initial icon
        initial_icon = _make_icon(False, False)

        menu = pystray.Menu(
            pystray.MenuItem("Open Fleet Admin",  self._open_ui,  default=True),
            pystray.MenuItem("Open Daemon API",   self._open_api),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem("Restart Daemon",    self._restart_daemon),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem("Quit",              self._quit),
        )

        self._icon = pystray.Icon(
            name="loralink-daemon",
            icon=initial_icon,
            title="LoRaLink Fleet — starting...",
            menu=menu,
        )

        # Start health polling in background
        poll_thread = threading.Thread(target=self._poll_loop, daemon=True)
        poll_thread.start()

        # Run tray (blocking)
        self._icon.run()


def main():
    tray = DaemonTray()
    tray.run()


if __name__ == "__main__":
    main()
