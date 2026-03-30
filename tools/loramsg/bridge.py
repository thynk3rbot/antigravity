"""
LMX Serial Bridge — reads LMX packets from USB serial, parses headers,
and calls back into the daemon with structured message events.
"""

import asyncio
import struct
import logging
from typing import Callable, Optional

logger = logging.getLogger("LMXBridge")

LMX_SYNC_0 = 0xAA
LMX_SYNC_1 = 0x4D
LMX_HEADER_SIZE = 12

# flags byte: [HopLimit:3][WantAck:1][MsgType:4]
LMX_MSG_TEXT = 0x0
LMX_MSG_ACK  = 0x1


def parse_header(data: bytes) -> Optional[dict]:
    """Parse a 12-byte LMX header from raw bytes. Returns None if invalid."""
    if len(data) < LMX_HEADER_SIZE:
        return None
    sync0, sync1, dest, src, packet_id, flags, hop_start, r0, r1 = struct.unpack_from(
        "<BBBBIBBxx", data, 0
    )
    if sync0 != LMX_SYNC_0 or sync1 != LMX_SYNC_1:
        return None
    return {
        "dest":      dest,
        "src":       src,
        "packet_id": packet_id,
        "hop_limit": (flags >> 5) & 0x07,
        "want_ack":  bool((flags >> 4) & 0x01),
        "msg_type":  flags & 0x0F,
        "hop_start": hop_start,
    }


class LMXSerialBridge:
    """
    Reads lines from serial (expecting LMX packets as hex-encoded strings
    prefixed with [LMX] for easy parsing alongside normal debug output).

    Devices should emit:
        [LMX] AA4D<rest-of-packet-as-hex>
    """

    def __init__(self, port: str, baudrate: int = 115200,
                 on_message: Optional[Callable] = None,
                 on_ack: Optional[Callable] = None):
        self.port = port
        self.baudrate = baudrate
        self.on_message = on_message  # (src, dest, packet_id, text, hops_used)
        self.on_ack = on_ack          # (packet_id,)
        self._running = False
        self._writer = None

    async def start(self):
        try:
            import serial_asyncio
        except ImportError:
            logger.error("serial_asyncio not installed: pip install pyserial-asyncio")
            return

        self._running = True
        logger.info(f"Opening serial {self.port} @ {self.baudrate}")
        try:
            reader, self._writer = await serial_asyncio.open_serial_connection(
                url=self.port, baudrate=self.baudrate
            )
            asyncio.create_task(self._read_loop(reader))
        except Exception as e:
            logger.error(f"Serial open failed: {e}")
            self._running = False

    async def _read_loop(self, reader):
        while self._running:
            try:
                line = await reader.readline()
                line = line.decode(errors="ignore").strip()
                if line.startswith("[LMX] "):
                    hexstr = line[6:].replace(" ", "")
                    try:
                        raw = bytes.fromhex(hexstr)
                        self._dispatch(raw)
                    except ValueError:
                        pass
            except Exception as e:
                logger.warning(f"Serial read error: {e}")
                await asyncio.sleep(1)

    def _dispatch(self, raw: bytes):
        hdr = parse_header(raw)
        if not hdr:
            return

        payload = raw[LMX_HEADER_SIZE:]
        msg_type = hdr["msg_type"]

        if msg_type == LMX_MSG_TEXT and self.on_message:
            text = payload.decode("utf-8", errors="replace")
            hops_used = hdr["hop_start"] - hdr["hop_limit"]
            asyncio.create_task(
                self.on_message(hdr["src"], hdr["dest"], hdr["packet_id"], text, hops_used)
            )
        elif msg_type == LMX_MSG_ACK and self.on_ack and len(payload) >= 4:
            acked_id = struct.unpack_from("<I", payload)[0]
            asyncio.create_task(self.on_ack(acked_id))

    async def send_text(self, dest: int, text: str):
        """Send MSG command over serial (device handles LMX encoding)."""
        if not self._writer:
            logger.warning("Serial not connected, cannot send")
            return
        cmd = f"MSG {dest:02X} {text}\n"
        self._writer.write(cmd.encode())
        await self._writer.drain()

    def stop(self):
        self._running = False
        if self._writer:
            self._writer.close()
