# magic Fleet Registry (Unambiguous)

This registry is driven by live network discovery. Identification is anchored to the **Full MAC Address** and **Last 4** for brevity. All nodes are peers in an Any2Any mesh.

| MAC (Last 4) | Friendly Name | IP (Current) | HW Class | OS Version | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **E6C8** | magic-Peer1V3 | 172.16.0.27 | V3 | 0.0.13 | [x] Online (Verified) |
| **A4B8** | magic-Peer4V4 | 172.16.0.29 | V4 | 0.0.12 | [x] Online |
| **97D4** | magic-Peer3V4 | 172.16.0. | V4 | 0.0.12 | [x] Online |
| **7E34** | magic-Peer5V2 | 172.16.0.30 | V4 | 0.0.12 | [x] Online |
| **FCC4** | magic-Peer2V3 | 172.16.0.26 | V3 | Unknown | [ ] Offline |
| **4568** | magic-Sentinel | 172.16.0.61 | T-Beam | 2.5.15 | [x] Online (Upstairs) |
| **TBD** | magic-OldSlaveV3 | Pending | V3 | Unknown | [ ] Offline |

### 6. LilyGo T-TWR (DO NOT FLASH)
- **Role**: Unknown/Pending
- **Hardware**: LilyGo T-TWR
| **TBD** | magic-LilyGo | Pending | LilyGo | Unknown | [ ] DO NOT FLASH |

---

## Fleet Identification Protocol
- **Discovery**: Use `python tools/discover_fleet.py` for mDNS census.
- **Unambiguous Primary Key**: Full MAC Address.
- **Hierarchical Roles**: None. All nodes are autonomous peers.
- **OTA Strategy**: Parallel or Sequential targeted by IP/MAC.
