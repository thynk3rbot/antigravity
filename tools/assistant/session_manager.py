import sqlite3
import logging
from datetime import datetime
from pathlib import Path
from typing import List, Dict, Optional

logger = logging.getLogger("SessionManager")

class SessionManager:
    def __init__(self, db_path: Path):
        self.db_path = db_path
        # Ensure parent directory exists
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self._init_db()

    def _init_db(self):
        """Initializes the SQLite database with sessions and messages tables."""
        conn = sqlite3.connect(self.db_path)
        try:
            cursor = conn.cursor()
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS sessions (
                    id TEXT PRIMARY KEY,
                    title TEXT,
                    domain TEXT,
                    created_at TEXT,
                    last_active TEXT
                )
            """)
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS messages (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    session_id TEXT,
                    role TEXT,
                    content TEXT,
                    created_at TEXT,
                    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
                )
            """)
            conn.commit()
        finally:
            conn.close()

    def create_session(self, session_id: str, title: str, domain: Optional[str] = None) -> str:
        """Creates a new chat session."""
        now = datetime.utcnow().isoformat()
        conn = sqlite3.connect(self.db_path)
        try:
            cursor = conn.cursor()
            cursor.execute(
                "INSERT INTO sessions (id, title, domain, created_at, last_active) VALUES (?, ?, ?, ?, ?)",
                (session_id, title, domain, now, now)
            )
            conn.commit()
            return session_id
        finally:
            conn.close()

    def add_message(self, session_id: str, role: str, content: str):
        """Adds a message to a session and updates last_active."""
        now = datetime.utcnow().isoformat()
        conn = sqlite3.connect(self.db_path)
        try:
            cursor = conn.cursor()
            cursor.execute(
                "INSERT INTO messages (session_id, role, content, created_at) VALUES (?, ?, ?, ?)",
                (session_id, role, content, now)
            )
            cursor.execute(
                "UPDATE sessions SET last_active = ? WHERE id = ?",
                (now, session_id)
            )
            conn.commit()
        finally:
            conn.close()

    def get_history(self, session_id: str, limit: int = 50) -> List[Dict]:
        """Retrieves chat history for a session."""
        conn = sqlite3.connect(self.db_path)
        conn.row_factory = sqlite3.Row
        try:
            cursor = conn.cursor()
            cursor.execute(
                "SELECT role, content, created_at FROM messages WHERE session_id = ? ORDER BY created_at ASC LIMIT ?",
                (session_id, limit)
            )
            return [dict(row) for row in cursor.fetchall()]
        finally:
            conn.close()

    def list_sessions(self, limit: int = 50) -> List[Dict]:
        """Lists all chat sessions."""
        conn = sqlite3.connect(self.db_path)
        conn.row_factory = sqlite3.Row
        try:
            cursor = conn.cursor()
            cursor.execute(
                "SELECT id, title, domain, created_at, last_active FROM sessions ORDER BY last_active DESC LIMIT ?",
                (limit,)
            )
            return [dict(row) for row in cursor.fetchall()]
        finally:
            conn.close()

    def delete_session(self, session_id: str):
        """Deletes a session and its messages."""
        conn = sqlite3.connect(self.db_path)
        try:
            cursor = conn.cursor()
            cursor.execute("DELETE FROM sessions WHERE id = ?", (session_id,))
            conn.commit()
        finally:
            conn.close()

    def update_session_title(self, session_id: str, title: str):
        """Updates the title of a session."""
        conn = sqlite3.connect(self.db_path)
        try:
            cursor = conn.cursor()
            cursor.execute("UPDATE sessions SET title = ? WHERE id = ?", (title, session_id))
            conn.commit()
        finally:
            conn.close()
