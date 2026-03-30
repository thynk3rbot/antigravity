"""
LMX Message Store — SQLite persistence for loramsg daemon.
Stores sent/received messages with delivery status.
"""

import sqlite3
from datetime import datetime
from pathlib import Path
from typing import List, Dict, Optional

DB_PATH = Path(__file__).parent / "data" / "messages.db"


def _conn():
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    c = sqlite3.connect(DB_PATH)
    c.row_factory = sqlite3.Row
    return c


def init():
    with _conn() as c:
        c.execute("""
            CREATE TABLE IF NOT EXISTS messages (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                packet_id   INTEGER,
                src         INTEGER,
                dest        INTEGER,
                direction   TEXT,        -- 'tx' or 'rx'
                content     TEXT,
                hops_used   INTEGER DEFAULT 0,
                status      TEXT DEFAULT 'sent',   -- sent | acked | failed
                created_at  TEXT
            )
        """)
        c.commit()


def save(packet_id: int, src: int, dest: int, direction: str,
         content: str, hops_used: int = 0) -> int:
    now = datetime.utcnow().isoformat()
    with _conn() as c:
        cur = c.execute(
            """INSERT INTO messages (packet_id, src, dest, direction, content, hops_used, status, created_at)
               VALUES (?, ?, ?, ?, ?, ?, 'sent', ?)""",
            (packet_id, src, dest, direction, content, hops_used, now)
        )
        c.commit()
        return cur.lastrowid


def ack(packet_id: int):
    with _conn() as c:
        c.execute("UPDATE messages SET status = 'acked' WHERE packet_id = ? AND direction = 'tx'",
                  (packet_id,))
        c.commit()


def fail(packet_id: int):
    with _conn() as c:
        c.execute("UPDATE messages SET status = 'failed' WHERE packet_id = ? AND direction = 'tx'",
                  (packet_id,))
        c.commit()


def recent(limit: int = 100) -> List[Dict]:
    with _conn() as c:
        rows = c.execute(
            "SELECT * FROM messages ORDER BY created_at DESC LIMIT ?", (limit,)
        ).fetchall()
    return [dict(r) for r in reversed(rows)]
