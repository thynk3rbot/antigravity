import threading
import time
import webbrowser
import json
import urllib.request
from pathlib import Path

try:
    import pystray
    from PIL import Image, ImageDraw
    TRAY_AVAILABLE = True
except ImportError:
    TRAY_AVAILABLE = False

SERVER_URL = "http://127.0.0.1:8300"
POLL_INTERVAL = 5

def _load_config():
    p = Path(__file__).parent / "config.json"
    if p.exists():
        with open(p) as f:
            return json.load(f)
    return {}

def _make_icon_image(server_ok: bool, ollama_ok: bool) -> "Image.Image":
    size = 64
    # Load branded icon if exists
    icon_path = Path(__file__).parent / "static" / "media" / "logo.png"
    if icon_path.exists():
        img = Image.open(icon_path).convert("RGBA").resize((size, size))
    else:
        # Fallback dark square
        img = Image.new("RGBA", (size, size), (20, 20, 40, 255))
        draw = ImageDraw.Draw(img)
        draw.rectangle([2, 2, size-3, size-3], outline=(0, 212, 255), width=2)

    draw = ImageDraw.Draw(img)
    dot_r = 8
    
    # Server dot (top-right)
    s_col = (0, 255, 136) if server_ok else (255, 68, 68)
    draw.ellipse([size-dot_r*2-2, 2, size-2, 2+dot_r*2], fill=(0,0,0))
    draw.ellipse([size-dot_r*2, 4, size-4, dot_r*2], fill=s_col)

    # Ollama dot (bottom-right)
    o_col = (0, 255, 136) if ollama_ok else (255, 68, 68)
    draw.ellipse([size-dot_r*2-2, size-dot_r*2-2, size-2, size-2], fill=(0,0,0))
    draw.ellipse([size-dot_r*2, size-dot_r*2, size-4, size-4], fill=o_col)

    return img

class AssistantTray:
    def __init__(self):
        self.icon = None
        self.running = True
        self.server_ok = False
        self.ollama_ok = False
        self.config = _load_config()
        self.app_name = self.config.get("branding", {}).get("app_name", "Assistant")

    def _check_health(self):
        try:
            with urllib.request.urlopen(f"{SERVER_URL}/health", timeout=2) as r:
                data = json.loads(r.read().decode())
                self.server_ok = True
                self.ollama_ok = data.get("ollama", False)
        except Exception:
            self.server_ok = False
            self.ollama_ok = False

    def _update_loop(self):
        while self.running:
            self._check_health()
            if self.icon:
                self.icon.icon = _make_icon_image(self.server_ok, self.ollama_ok)
                status = f"Srv: {'✓' if self.server_ok else '✗'} | AI: {'✓' if self.ollama_ok else '✗'}"
                self.icon.title = f"{self.app_name} — {status}"
            time.sleep(POLL_INTERVAL)

    def _open_ui(self, icon=None, item=None):
        webbrowser.open(SERVER_URL)

    def _trigger_sync(self, icon=None, item=None):
        # Trigger ingestion for all domains (simple implementation)
        try:
            # We just hit the endpoint for the first domain for now as a demo
            req = urllib.request.Request(f"{SERVER_URL}/api/ingest", 
                                        data=json.dumps({"domain": "garden"}).encode(),
                                        headers={"Content-Type": "application/json"})
            urllib.request.urlopen(req, timeout=5)
        except Exception:
            pass

    def _quit(self, icon=None, item=None):
        self.running = False
        if self.icon:
            self.icon.stop()

    def run(self):
        if not TRAY_AVAILABLE:
            print("Tray dependencies missing. Run: pip install pystray Pillow")
            return

        menu = pystray.Menu(
            pystray.MenuItem(f"Open {self.app_name}", self._open_ui, default=True),
            pystray.MenuItem("Sync Knowledge Base", self._trigger_sync),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem("Quit", self._quit)
        )

        self.icon = pystray.Icon(
            "loralink-assistant",
            _make_icon_image(False, False),
            f"{self.app_name} — starting...",
            menu
        )

        threading.Thread(target=self._update_loop, daemon=True).start()
        self.icon.run()

if __name__ == "__main__":
    AssistantTray().run()
