import sqlite3
from pathlib import Path
from typing import Optional, List
from tools.daemon.models import Message, MessageStatus, Transport


class MessageQueue:
  """SQLite-backed message queue for daemon persistence"""

  def __init__(self, db_path: Path):
    self.db_path = db_path
    self._init_db()

  def _get_connection(self):
    """Get a database connection with proper timeout"""
    conn = sqlite3.connect(str(self.db_path), timeout=5.0)
    conn.isolation_level = None
    return conn

  def _init_db(self):
    """Initialize SQLite schema"""
    conn = self._get_connection()
    try:
      cursor = conn.cursor()
      cursor.execute("""
        CREATE TABLE IF NOT EXISTS messages (
          id TEXT PRIMARY KEY,
          src TEXT,
          dest TEXT,
          command TEXT,
          status TEXT,
          created_at REAL,
          sent_at REAL,
          acked_at REAL,
          transport_used TEXT,
          retry_count INTEGER
        )
      """)
      cursor.execute("""
        CREATE INDEX IF NOT EXISTS idx_status ON messages(status)
      """)
      cursor.execute("""
        CREATE INDEX IF NOT EXISTS idx_dest ON messages(dest)
      """)
      conn.commit()
    finally:
      conn.close()

  def save_message(self, msg: Message) -> None:
    """Save or update message in queue"""
    conn = self._get_connection()
    try:
      cursor = conn.cursor()
      cursor.execute("""
        INSERT OR REPLACE INTO messages
        (id, src, dest, command, status, created_at, sent_at, acked_at, transport_used, retry_count)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
      """, (
        msg.id, msg.src, msg.dest, msg.command,
        msg.status.value, msg.created_at, msg.sent_at, msg.acked_at,
        msg.transport_used.value if msg.transport_used else None,
        msg.retry_count
      ))
      conn.commit()
    finally:
      conn.close()

  def get_message(self, msg_id: str) -> Optional[Message]:
    """Retrieve message by ID"""
    conn = self._get_connection()
    try:
      cursor = conn.cursor()
      cursor.execute("SELECT * FROM messages WHERE id = ?", (msg_id,))
      row = cursor.fetchone()
      if not row:
        return None
      return self._row_to_message(row)
    finally:
      conn.close()

  def list_messages(self, status: Optional[MessageStatus] = None, dest: Optional[str] = None, limit: int = 100) -> List[Message]:
    """List messages by filter"""
    conn = self._get_connection()
    try:
      cursor = conn.cursor()
      query = "SELECT * FROM messages WHERE 1=1"
      params = []

      if status:
        query += " AND status = ?"
        params.append(status.value)

      if dest:
        query += " AND dest = ?"
        params.append(dest)

      query += " ORDER BY created_at DESC LIMIT ?"
      params.append(limit)

      cursor.execute(query, params)
      rows = cursor.fetchall()
      return [self._row_to_message(row) for row in rows]
    finally:
      conn.close()

  def update_status(self, msg_id: str, status: MessageStatus) -> None:
    """Update message status"""
    conn = self._get_connection()
    try:
      cursor = conn.cursor()
      cursor.execute("UPDATE messages SET status = ? WHERE id = ?", (status.value, msg_id))
      conn.commit()
    finally:
      conn.close()

  def _row_to_message(self, row) -> Message:
    """Convert SQLite row to Message object"""
    return Message(
      id=row[0],
      src=row[1],
      dest=row[2],
      command=row[3],
      status=MessageStatus(row[4]),
      created_at=row[5],
      sent_at=row[6],
      acked_at=row[7],
      transport_used=Transport(row[8]) if row[8] else None,
      retry_count=row[9]
    )
