import serial
import serial.tools.list_ports
import time

ports = [p.device for p in serial.tools.list_ports.comports()]
print(f"Brute-forcing REBOOT on ports: {ports}")

for p in ports:
    try:
        print(f"Trying {p}...")
        with serial.Serial(p, 115200, timeout=1) as s:
            s.write(b"\nREBOOT\n")
            time.sleep(0.1)
            # Read response if any
            resp = s.read_all().decode(errors='ignore')
            print(f"  Response: {resp.strip()}")
    except Exception as e:
        print(f"  Failed {p}: {e}")
