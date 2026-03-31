import json
from typing import Dict
import paho.mqtt.client as mqtt
from textual.app import App, ComposeResult
from textual.widgets import Header, Footer, Tree, DataTable, Static, Input
from textual.containers import Horizontal, Vertical
from textual.binding import Binding
from rich.text import Text

class Magic(App):
    """
    Terrific TUI for Magic.
    Visualize real-time LVC status with 'Update-is-Replace' awareness.
    """
    CSS = """
    Screen {
        background: #1a1b26;
    }
    #sidebar {
        width: 30%;
        border-right: solid #24283b;
        background: #1a1b26;
    }
    #main {
        width: 70%;
        padding: 1;
    }
    #lvc-table {
        height: 1fr;
        border: solid #24283b;
    }
    #search-box {
        dock: top;
        margin: 1;
    }
    """

    BINDINGS = [
        Binding("q", "quit", "Quit"),
        Binding("r", "refresh", "Refresh"),
        Binding("c", "clear", "Clear Cache"),
    ]

    def __init__(self, host="localhost", port=1883):
        super().__init__()
        self.host = host
        self.port = port
        self.lvc_cache: Dict[str, Dict] = {}  # Local LVC copy for UI interaction
        self.current_subject: str = ""
        
        # MQTT
        self.client = mqtt.Client("Magic")
        self.client.on_connect = self.on_mqtt_connect
        self.client.on_message = self.on_mqtt_message

    def on_mqtt_connect(self, client, userdata, flags, rc):
        if rc == 0:
            client.subscribe("MagicCache/#")
            self.notify("Connected to Magic Bus", severity="information")
        else:
            self.notify(f"Connection failed: {rc}", severity="error")

    def on_mqtt_message(self, client, userdata, msg):
        """Update local LVC and signal UI refresh."""
        try:
            topic = msg.topic
            subject = topic.split('/', 1)[-1].replace('/', '.')
            payload = json.loads(msg.payload.decode())
            
            if subject not in self.lvc_cache:
                self.lvc_cache[subject] = {}
                # Update tree on first discovery
                self.call_from_thread(self.update_tree, subject)
            
            # Merge KV (Update-is-Replace)
            self.lvc_cache[subject].update(payload)
            
            # Refresh table if this is the active subject
            if subject == self.current_subject:
                self.call_from_thread(self.refresh_lvc_table)
                
        except Exception:
            pass # Silent error in TUI data stream

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        with Horizontal():
            with Vertical(id="sidebar"):
                yield Input(placeholder="Search Subjects...", id="search-box")
                yield Tree("Magic", id="subject-tree")
            with Vertical(id="main"):
                yield Static(id="active-subject", content="Select a subject from the tree")
                yield DataTable(id="lvc-table")
        yield Footer()

    def on_mount(self) -> None:
        table = self.query_one("#lvc-table", DataTable)
        table.add_columns("Field", "Value")
        table.cursor_type = "row"
        
        # Start MQTT loop in background
        self.client.connect_async(self.host, self.port, 60)
        self.client.loop_start()

    def update_tree(self, subject: str):
        tree = self.query_one("#subject-tree")
        parts = subject.split('.')
        current_node = tree.root
        
        for part in parts:
            # Check if child already exists
            found = False
            for child in current_node.children:
                if str(child.label) == part:
                    current_node = child
                    found = True
                    break
            if not found:
                current_node = current_node.add(part, expand=True)

    def on_tree_node_selected(self, event: Tree.NodeSelected):
        """Build full subject path from tree node selection."""
        node = event.node
        parts = []
        while node.parent is not None:
            parts.append(str(node.label))
            node = node.parent
        
        self.current_subject = ".".join(reversed(parts))
        self.query_one("#active-subject").update(f"SUBJECT: [bold cyan]{self.current_subject}[/bold cyan]")
        self.refresh_lvc_table()

    def refresh_lvc_table(self):
        table = self.query_one("#lvc-table", DataTable)
        table.clear()
        
        state = self.lvc_cache.get(self.current_subject, {})
        for k, v in state.items():
            # Highlight numeric changes? For high speed, just simple table update for now
            table.add_row(Text(k, style="bold"), str(v))

    def action_refresh(self):
        self.refresh_lvc_table()

    def action_clear(self):
        self.lvc_cache.clear()
        self.query_one("#subject-tree").root.remove_children()
        self.refresh_lvc_table()

    def on_unmount(self) -> None:
        self.client.loop_stop()
        self.client.disconnect()

if __name__ == "__main__":
    app = Magic()
    app.run()
