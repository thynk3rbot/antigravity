import asyncio
import time
from typing import List, Dict, Any, Callable, Awaitable

# Core regression suite
DEFAULT_COMMANDS = [
    "STATUS", "RADIO", "TASKS", "UPTIME", "NODES",
    "READ LED", "READ 6", "LED ON", "LED OFF", "BLINK", "SCHED LIST"
]

class TestEngine:
    """
    Modular test engine that runs a suite of commands against a target.
    The 'executor' is a callable that takes a command string and returns (ok, response_text).
    """
    def __init__(self, executor: Callable[[str], Awaitable[tuple[bool, str]]]):
        self.executor = executor
        self.results = []

    async def run_single_test(self, cmd: str, timeout: float = 5.0) -> Dict[str, Any]:
        """Runs a single test command and returns a result dict."""
        start_time = time.perf_counter()
        result = {
            "cmd": cmd,
            "status": "ERROR",
            "latency": 0.0,
            "info": "",
            "timestamp": time.time()
        }

        try:
            # We assume the executor handles its own timeout internally if needed,
            # but we can wrap it here as a safety net.
            ok, response_text = await asyncio.wait_for(self.executor(cmd), timeout=timeout)
            elapsed = (time.perf_counter() - start_time) * 1000
            result["latency"] = round(elapsed, 2)

            if ok:
                # Basic health check: if info looks like a timeout or empty, it might be a soft fail
                if not response_text.strip():
                   result["status"] = "FAIL"
                   result["info"] = "Empty response"
                else:
                   result["status"] = "PASS"
                   result["info"] = response_text[:200]
            else:
                result["status"] = "FAIL"
                result["info"] = response_text[:200]

        except asyncio.TimeoutError:
            elapsed = (time.perf_counter() - start_time) * 1000
            result["latency"] = round(elapsed, 2)
            result["status"] = "FAIL"
            result["info"] = f"Timeout after {timeout}s"
        except Exception as e:
            elapsed = (time.perf_counter() - start_time) * 1000
            result["latency"] = round(elapsed, 2)
            result["status"] = "ERROR"
            result["info"] = str(e)

        return result

    async def run_suite(self, commands: List[str] = None, delay: float = 0.5, timeout_per_cmd: float = 12.0) -> List[Dict[str, Any]]:
        """Runs multiple commands in sequence."""
        if commands is None:
            commands = DEFAULT_COMMANDS
        
        self.results = []
        for cmd in commands:
            res = await self.run_single_test(cmd, timeout=timeout_per_cmd)
            self.results.append(res)
            if delay > 0:
                await asyncio.sleep(delay)
        
        return self.results
