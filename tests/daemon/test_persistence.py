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

        queue.update_status(msg.id, MessageStatus.SENT)
        loaded = queue.get_message(msg.id)
        assert loaded.status == MessageStatus.SENT
