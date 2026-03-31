"""
find_ports.py — List all connected ESP32/Magic devices and their COM ports.

Usage:
    python tools/find_ports.py

Output example:
    COM19  303A:1001  44:1B:F6:70:A4:B8  USB Serial Device  [ESP32-S3 Native]
    COM5   10C4:EA60  N/A                 CP2102 USB to UART [ESP32 V2]
"""

import serial.tools.list_ports

ESP_VIDS = {
    "303A": "ESP32-S3 (Native USB)",
    "10C4": "CP210x (V2 / FTDI)",
    "0403": "FTDI (USB-Serial)",
    "1A86": "CH340 (USB-Serial)",
    "2341": "Arduino",
}

def find_ports():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No serial devices found.")
        return

    print(f"\n{'PORT':<8} {'VID:PID':<12} {'SERIAL / MAC':<24} {'CHIP':<28} DESCRIPTION")
    print("-" * 100)

    for p in sorted(ports, key=lambda x: x.device):
        vid_str = f"{p.vid:04X}" if p.vid else "????"
        pid_str = f"{p.pid:04X}" if p.pid else "????"
        vid_pid = f"{vid_str}:{pid_str}"
        serial_num = p.serial_number or "N/A"
        chip = ESP_VIDS.get(vid_str, "Unknown")
        desc = p.description or ""
        print(f"{p.device:<8} {vid_pid:<12} {serial_num:<24} {chip:<28} {desc}")

    print()

if __name__ == "__main__":
    find_ports()
