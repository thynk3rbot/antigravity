import pytest
import tempfile
from pathlib import Path
from tools.daemon.persistence import MessageQueue
from tools.daemon.models import Message, MessageStatus, Transport


def test_message_queue_save_and_load():
    with tempfile.TemporaryDirectory() as tmpdir:
        queue = MessageQueue(Path(tmpdir) / "queue.db")

        msg = Message(dest="node1", command="GPIO 5 HIGH")
        queue.save_message(msg)

        loaded = queue.get_message(msg.id)
        assert loaded.id == msg.id
        assert loaded.dest == "node1"
        assert loaded.command == "GPIO 5 HIGH"


def test_message_queue_list_by_status():
    with tempfile.TemporaryDirectory() as tmpdir:
        queue = MessageQueue(Path(tmpdir) / "queue.db")

        msg1 = Message(dest="node1", command="CMD1", status=MessageStatus.QUEUED)
        msg2 = Message(dest="node2", command="CMD2", status=MessageStatus.SENT)

        queue.save_message(msg1)
        queue.save_message(msg2)

        queued = queue.list_messages(status=MessageStatus.QUEUED)
        assert len(queued) == 1
        assert queued[0].command == "CMD1"


def test_message_queue_update_status():
    with tempfile.TemporaryDirectory() as tmpdir:
        queue = MessageQueue(Path(tmpdir) / "queue.db")

        msg = Message(dest="node1", command="CMD")
        queue.save_message(msg)

        result = queue.update_status(msg.id, MessageStatus.SENT)
        assert result is True
        loaded = queue.get_message(msg.id)
        assert loaded.status == MessageStatus.SENT


def test_message_queue_invalid_enum_in_db():
    """Test that corrupted enum values raise informative errors"""
    with tempfile.TemporaryDirectory() as tmpdir:
        queue = MessageQueue(Path(tmpdir) / "queue.db")

        # Manually insert invalid status value into database
        import sqlite3
        conn = sqlite3.connect(str(queue.db_path))
        try:
            cursor = conn.cursor()
            cursor.execute(
                "INSERT INTO messages (id, src, dest, command, status, created_at, sent_at, acked_at, transport_used, retry_count) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                ("msg1", "src1", "dest1", "CMD", "INVALID_STATUS", 0, None, None, None, 0)
            )
            conn.commit()
        finally:
            conn.close()

        # Attempting to load should raise ValueError with helpful message
        with pytest.raises(ValueError, match="Corrupted database"):
            queue.get_message("msg1")


def test_message_queue_update_status_not_found():
    """Test that update_status returns False when message not found"""
    with tempfile.TemporaryDirectory() as tmpdir:
        queue = MessageQueue(Path(tmpdir) / "queue.db")

        # Try to update non-existent message
        result = queue.update_status("nonexistent", MessageStatus.SENT)
        assert result is False
