#!/usr/bin/env python3
"""
Daemon launcher script - handles module paths and startup.
Usage: python run.py [--port 8001] [--mqtt-broker localhost:1883]
"""

import sys
import os

# Add src directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'src'))

# Now we can run the main daemon
if __name__ == "__main__":
    from main import main
    import asyncio
    asyncio.run(main())
