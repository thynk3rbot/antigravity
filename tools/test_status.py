import serial
import time
import sys

s = serial.Serial()
s.port = 'COM19'
s.baudrate = 115200
s.timeout = 1
s.setDTR(False)
s.setRTS(False)
s.open()
time.sleep(1)
s.write(b'STATUS\n')
s.flush()
start = time.time()
while time.time() - start < 10:
    line = s.readline().decode('utf-8', errors='ignore').strip()
    if line:
        if line.startswith('{'):
            import json
            try:
                data = json.loads(line)
                print(f"BAT_PCT={data.get('bat_pct')}, VEXT={data.get('vext')}, UPTIME={data.get('uptime')}")
                sys.exit(0)
            except:
                pass
        else:
            print("LOG:", line)
s.close()
