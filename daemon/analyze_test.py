#!/usr/bin/env python3
"""
Fleet Test Analysis Script

Quick analysis of daemon logs to identify test success/failures.
Usage: python analyze_test.py daemon.log

Looks for:
- Device registration events
- Command publishing
- ACK receipts
- Error patterns
"""

import sys
import re
from collections import defaultdict
from datetime import datetime


def parse_log_line(line):
    """Parse a log line into timestamp, level, component, and message."""
    # Format: [2026-03-26 14:50:15] daemon.mesh_router - INFO - [Peer] node-30 registered
    pattern = r'\[([^\]]+)\]\s+(\S+)\s+-\s+(\w+)\s+-\s+(.*)'
    match = re.match(pattern, line)
    if match:
        return {
            'timestamp': match.group(1),
            'component': match.group(2),
            'level': match.group(3),
            'message': match.group(4)
        }
    return None


def analyze_log(filename):
    """Analyze log file for test results."""
    print(f"\n{'='*70}")
    print(f"Phase 50 Fleet Test Analysis")
    print(f"{'='*70}\n")

    with open(filename, 'r') as f:
        lines = f.readlines()

    # Parse all lines
    events = []
    for line in lines:
        parsed = parse_log_line(line.strip())
        if parsed:
            events.append(parsed)

    if not events:
        print("ERROR: No valid log lines found. Check log format.")
        return

    print(f"Log contains {len(events)} events\n")

    # Device registration
    print("DEVICE REGISTRATION")
    print("-" * 70)
    registrations = [e for e in events if '[Peer]' in e['message'] and 'registered' in e['message']]
    if registrations:
        for reg in registrations:
            print(f"  {reg['timestamp']}: {reg['message']}")
    else:
        print("  WARNING: No device registrations found!")
    print(f"  Total registered: {len(registrations)}\n")

    # Commands published
    print("COMMANDS PUBLISHED")
    print("-" * 70)
    published = [e for e in events if '[Cmd]' in e['message'] and 'published' in e['message']]
    if published:
        for pub in published:
            # Extract cmd_id
            match = re.search(r'(\w{8})', pub['message'])
            cmd_id = match.group(1) if match else "?"
            print(f"  {pub['timestamp']}: {pub['message'][:60]}...")
    else:
        print("  INFO: No commands published yet")
    print(f"  Total published: {len(published)}\n")

    # ACKs received
    print("ACKS RECEIVED")
    print("-" * 70)
    acks = [e for e in events if '[Ack]' in e['message']]
    acks_ok = [a for a in acks if 'SUCCESS' in a['message'] or 'completed' in a['message']]
    acks_fail = [a for a in acks if 'FAILED' in a['message'] or 'failed' in a['message']]

    if acks_ok:
        for ack in acks_ok:
            print(f"  [OK] {ack['timestamp']}: {ack['message']}")
    if acks_fail:
        for ack in acks_fail:
            print(f"  [FAIL] {ack['timestamp']}: {ack['message']}")
    if not acks:
        print("  INFO: No ACKs received yet")
    print(f"  Success: {len(acks_ok)}, Failed: {len(acks_fail)}\n")

    # Errors
    print("ERRORS & WARNINGS")
    print("-" * 70)
    errors = [e for e in events if e['level'] in ['ERROR', 'WARNING']]
    if errors:
        for err in errors[:10]:  # Show first 10
            print(f"  [{err['level']}] {err['timestamp']}: {err['message'][:60]}...")
        if len(errors) > 10:
            print(f"  ... and {len(errors) - 10} more errors")
    else:
        print("  No errors or warnings\n")
    print(f"  Total: {len(errors)}\n")

    # Summary
    print("SUMMARY")
    print("-" * 70)
    print(f"  Devices registered:   {len(registrations)}")
    print(f"  Commands published:   {len(published)}")
    print(f"  ACKs successful:      {len(acks_ok)}")
    print(f"  ACKs failed:          {len(acks_fail)}")
    print(f"  Total errors:         {len(errors)}")

    # Test result
    print("\nTEST RESULT")
    print("-" * 70)
    if len(registrations) >= 2 and len(acks_ok) > 0:
        print("  ✓ PASS: Devices registered + commands successful")
    elif len(registrations) >= 2:
        print("  ⚠ PARTIAL: Devices registered but no successful commands")
    elif len(registrations) > 0:
        print("  ⚠ PARTIAL: Some devices registered")
    else:
        print("  ✗ FAIL: No devices registered")

    print(f"\n{'='*70}\n")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python analyze_test.py <logfile>")
        print("Example: python analyze_test.py daemon.log")
        sys.exit(1)

    logfile = sys.argv[1]
    try:
        analyze_log(logfile)
    except FileNotFoundError:
        print(f"ERROR: Log file not found: {logfile}")
        sys.exit(1)
    except Exception as e:
        print(f"ERROR: {e}")
        sys.exit(1)
