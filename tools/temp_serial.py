import serial
import time
import sys

port = 'COM7'
baud = 9600

try:
    print(f"Connecting to {port} at {baud} baud...")
    ser = serial.Serial(port, baud, timeout=1)
    
    print("Listening for 15 seconds...")
    start_time = time.time()
    
    while time.time() - start_time < 15:
        line = ser.readline()
        if line:
            try:
                print(line.decode('utf-8', errors='replace').strip())
            except Exception as e:
                print(f"[Binary/Garbage Data]: {line}")
                
    ser.close()
    print("\n[Done Reading]")
except Exception as e:
    print(f"Failed to connect or read: {e}")
