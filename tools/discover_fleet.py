import socket
import time
from zeroconf import ServiceBrowser, Zeroconf

class MagicDiscovery:
    def __init__(self):
        self.devices = []

    def remove_service(self, zc, type_, name):
        pass

    def add_service(self, zc, type_, name):
        info = zc.get_service_info(type_, name)
        if info:
            ip = socket.inet_ntoa(info.addresses[0])
            mac = info.properties.get(b'mac', b'Unknown').decode()
            hw = info.properties.get(b'hw', b'Unknown').decode()
            ver = info.properties.get(b'ver', b'Unknown').decode()
            self.devices.append({
                "name": name.split('.')[0],
                "ip": ip,
                "mac": mac,
                "hw": hw,
                "ver": ver
            })

    def update_service(self, zc, type_, name):
        pass

if __name__ == "__main__":
    zeroconf = Zeroconf()
    discovery = MagicDiscovery()
    browser = ServiceBrowser(zeroconf, "_http._tcp.local.", discovery)
    print("Scanning for Magic devices (5s)...")
    time.sleep(5)
    zeroconf.close()
    
    if not discovery.devices:
        print("No devices found via mDNS.")
    else:
        print(f"\nDiscovered {len(discovery.devices)} devices:")
        for dev in discovery.devices:
            print(f"- {dev['name']}: {dev['ip']} (MAC: {dev['mac']}, HW: {dev['hw']}, Version: {dev['ver']})")
