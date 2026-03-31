import threading
import webbrowser
import os
from pathlib import Path
import pystray
from PIL import Image
import logging

logger = logging.getLogger(__name__)

class TrayManager:
    """
    Visual System Tray (NotifyIcon) for Magic.
    Provides infrastructure awareness and recovery tools.
    """
    def __init__(self, daemon):
      self.daemon = daemon
      self.icon = None
      self._thread = None

    def create_icon_image(self):
      """Load the official Magic Octopus logo."""
      try:
        icon_path = Path(__file__).parent.parent.parent / "tools" / "webapp" / "static" / "media" / "magic_icon.png"
        if icon_path.exists():
          return Image.open(str(icon_path))
      except Exception as e:
        logger.warning(f"[Tray] Failed to load logo: {e}")
            
      return Image.new('RGB', (64, 64), (0, 255, 255))

    def on_open_dashboard(self, icon, item):
      webbrowser.open(f"http://localhost:{self.daemon.port}")

    def on_launch_docker(self, icon, item):
      """Trigger Docker Desktop launch from the tray."""
      self.daemon.infra.launch_docker_desktop()

    def on_restart_infra(self, icon, item):
      logger.info("[Tray) User requested infra restart.")
      import asyncio
      loop = asyncio.get_event_loop()
      asyncio.run_coroutine_threadsafe(self.daemon.infra.restart(), loop)

    def on_exit(self, icon, item):
      logger.info("[Tray) User requested exit via taskbar.")
      if self.icon:
        self.icon.stop()
      import asyncio
      asyncio.run_coroutine_threadsafe(self.daemon.shutdown(), asyncio.get_event_loop())

    def update_menu(self):
      """Dynamic menu based on Magic service and infrastructure status."""
      status = self.daemon.infra.status()
      svc_status = self.daemon.services.status_all()
      
      engine_up = status.get("engine_ready", False)
      infra_up = status.get("ready", False)
      lvc_up = svc_status.get("magic_lvc", {"running": False})["running"]
      
      engine_icon = "●" if engine_up else "⚠️"
      infra_icon = "●" if infra_up else "○"
      lvc_icon = "●" if lvc_up else "○"
      
      menu_items = [
        pystray.MenuItem("Magic Dashboard", self.on_open_dashboard, default=True),
        pystray.Menu.SEPARATOR
      ]
      
      if not engine_up:
        menu_items.append(pystray.MenuItem("⚠️ Start Docker Desktop", self.on_launch_docker))
        menu_items.append(pystray.Menu.SEPARATOR)
        
      menu_items.extend([
        pystray.MenuItem(f"{engine_icon} Docker Engine", lambda: None, enabled=False),
        pystray.MenuItem(f"{infra_icon} Magic Bus/DB", lambda: None, enabled=False),
        pystray.MenuItem(f"{lvc_icon} Magic LVC Store", lambda: None, enabled=False),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem("Restart Services", lambda: self.daemon.services.start_auto_services()),
        pystray.MenuItem("Restart Infrastructure", self.on_restart_infra),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem("Exit", self.on_exit)
      ])
      
      return pystray.Menu(*menu_items)

    def _update_ui_loop(self):
      """Background loop to refresh tray tooltips and menu status."""
      while self.icon and self.icon.visible:
        try:
          status = self.daemon.infra.status()
          if not status.get("engine_ready", False):
            self.icon.title = "Magic (OFFLINE - Docker Stop)"
          elif not status.get("ready", False):
            self.icon.title = "Magic (Starting Infrastructure...)"
          else:
            self.icon.title = "Magic (All Systems Nominal)"
          
          # Refresh menu state
          self.icon.menu = self.update_menu()
        except:
          pass
        time.sleep(5)

    def run(self):
      self.icon = pystray.Icon(
        "Magic",
        self.create_icon_image(),
        menu=self.update_menu(),
        title="Magic (Initializing...)"
      )
      # Launch separate UI refreshing thread since icon.run() blocks
      threading.Thread(target=self._update_ui_loop, daemon=True).start()
      self.icon.run()

    def start(self):
      self._thread = threading.Thread(target=self.run, daemon=True)
      self._thread.start()
      logger.info("[Tray] Magic icon initialized.")

    def stop(self):
      if self.icon:
        self.icon.stop()
