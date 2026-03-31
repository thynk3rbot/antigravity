@echo off
TITLE Magic — Sovereign Mesh Control
color 0D
cls

echo.
echo   ================================================
echo    M A G I C   Fleet Control Center
echo    Sovereign Connectivity Platform
echo   ================================================
echo.

set ROOT=%~dp0
cd /d "%ROOT%"

echo [1/4] Starting MQTT Broker (mosquitto port 1883)...
start "Magic MQTT Broker" cmd /k "mosquitto -c mosquitto.conf"
timeout /t 2 /nobreak >nul

echo [2/4] Starting Magic Daemon (API port 8001, Dashboard port 8000)...
start "Magic Daemon" cmd /k "python daemon\src\main.py"
timeout /t 3 /nobreak >nul

echo [3/4] Starting Magic Messenger Bridge (port 8400)...
start "Magic Messenger" cmd /k "python tools\loramsg\server.py"
timeout /t 1 /nobreak >nul

echo [4/4] Opening Magic Dashboard...
start http://localhost:8000

echo.
echo   ================================================
echo    Magic is running.
echo   ================================================
echo.
echo    Dashboard:     http://localhost:8000
echo    Daemon API:    http://localhost:8001
echo    AI Assistant:  http://localhost:8300
echo    Messenger:     http://localhost:8400
echo    MQTT:          localhost:1883
echo.
echo    To flash firmware:  pio run -t upload -e heltec_v4
echo    To monitor serial:  pio device monitor -b 115200
echo.
echo   Close individual windows to stop each service.
pause
