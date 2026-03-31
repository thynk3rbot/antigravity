#!/usr/bin/env python3
"""
simulator.py — Magic Digital Twin / Local Process Simulator Bridge
Connects the webapp to the native C++ firmware runner.
"""

import sys
import os
import json
import asyncio
import subprocess
import time
from pathlib import Path
from typing import Optional
import shlex

class SimulatorBridge:
    def __init__(self, binary_path: str):
        self.binary_path = binary_path
        self._proc: Optional[asyncio.subprocess.Process] = None
        self._running = False
        self._last_status = {}
        self.line_queue: asyncio.Queue = asyncio.Queue()

    async def start(self):
        if self._running: return
        print(f"[Sim] Starting: {self.binary_path}")
        try:
            # Handle commands with arguments
            args = shlex.split(self.binary_path)
            self._proc = await asyncio.create_subprocess_exec(
                args[0],
                *args[1:],
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
                stdin=asyncio.subprocess.PIPE
            )
            self._running = True
            asyncio.create_task(self._read_stdout())
            asyncio.create_task(self._read_stderr())
        except Exception as e:
            print(f"[Sim] Failed to start: {e}")

    async def stop(self):
        if not self._running or not self._proc: return
        print("[Sim] Stopping simulator...")
        self._proc.terminate()
        await self._proc.wait()
        self._running = False
        self._proc = None

    async def _read_stdout(self):
        while self._running and self._proc and self._proc.stdout:
            line = await self._proc.stdout.readline()
            if not line: break
            text = line.decode().strip()
            # Try to parse JSON status updates from C++
            if text.startswith('{'):
                try:
                    self._last_status = json.loads(text)
                except: pass
            else:
                await self.line_queue.put(text)
                print(f"[Sim-OUT] {text}")

    async def _read_stderr(self):
        while self._running and self._proc and self._proc.stderr:
            line = await self._proc.stderr.readline()
            if not line: break
            print(f"[Sim-ERR] {line.decode().strip()}")

    async def send_command(self, cmd: str):
        if not self._running or not self._proc or not self._proc.stdin:
            return False
        try:
            self._proc.stdin.write((cmd + "\n").encode())
            await self._proc.stdin.drain()
            return True
        except:
            return False

    def get_status(self):
        return self._last_status or {"online": self._running, "uptime": 0, "pins": []}

# Simple standalone runner for testing
if __name__ == "__main__":
    # Path to native binary
    # Default PlatformIO path: .pio/build/native/program.exe (on Windows)
    bin_path = str(Path(__file__).parent.parent / "firmware" / "v2" / ".pio" / "build" / "native" / "program.exe")
    bridge = SimulatorBridge(bin_path)
    
    async def run():
        await bridge.start()
        while True:
            await asyncio.sleep(1)
            print(f"Heartbeat: {bridge.get_status()}")

    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        pass
