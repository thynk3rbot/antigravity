# STARTUP: Magic

Starting **Magic** now automatically orchestrates the entire ecosystem, including the Docker-based infrastructure.

## 🌐 Networking & Ports

| Component | Port | Description |
| :--- | :--- | :--- |
| **Magic Bus** | `1883` | MQTT Broker (Mosquitto) |
| **Magic DB** | `5432` | PostgreSQL (SQLAlchemy Target) |
| **Magic Dashboard** | `8000` | User Management Interface |
| **Magic API** | `8001` | Core REST API & Router Engine |

---

## 🏗️ 0. Pre-Flight Check (Host OS)

1. **Docker Desktop**: Must be running. Ensure the "Docker Desktop" engine is "Ready" in the dashboard.
2. **Ports**: Ensure ports `1883, 5432, 8000, 8001` are free.

---

## 🐙 1. The Singular Boot Command

You no longer need to run `docker-compose` manually. Simply start **Magic**:

```powershell
# Open ONE terminal
cd daemon\src
python main.py
```

**Wait for the icon (🐙) to appear in the Windows System Tray.**

---

## 🖥️ 2. Control & Monitoring

1. **Dashboard**: Open [http://localhost:8000](http://localhost:8000).
2. **Magic Client (Recommended)**:
   Launch the branded terminal to observe real-time mesh activity:
   ```powershell
   # Launch from repo root
   .\magic_client.bat
   ```
   *Features: 🐙 branding, JSON syntax highlighting, and live streams.*

3. **Magic Console (TUI)**:
   Advanced view of the LVC state tree:
   ```powershell
   python daemon/src/console.py
   ```

4. **Magic Tray**: Right-click the 🐙 icon for health status and infra recovery.

---

## 🛠️ Troubleshooting

- **Docker Connectivity**: If Magic reports a timeout, ensure Docker Desktop is not still "Starting...".
- **Python Dependencies**: If the client fails to launch, run `pip install -r daemon/requirements.txt`.

---

**Magic Architecture v3.0**
"Update-is-Replace"
Unified Infrastructure & Native Observable Client
Addressing: Subject-Based (e.g. IMDF.QUOTE.MSFT)
