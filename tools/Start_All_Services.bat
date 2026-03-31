@echo off
TITLE Magic — All Services
color 0D
cls

echo.
echo   ================================================
echo    Magic  —  All Services
echo   ================================================
echo.

set ROOT=%~dp0..
cd /d "%ROOT%"

echo [1/4] MQTT Broker (port 1883)...
start "Magic MQTT" cmd /k "mosquitto -v -c mosquitto.conf"
timeout /t 2 /nobreak >nul

echo [2/4] Magic Daemon (API :8001, Dashboard :8000)...
start "Magic Daemon" cmd /k "daemon\start_daemon.bat"
timeout /t 3 /nobreak >nul

echo [3/4] Magic Messenger Bridge (port 8400)...
start "Magic Messenger" cmd /k "python tools\loramsg\server.py"
timeout /t 1 /nobreak >nul

echo [4/4] AI Assistant (port 8300)...
start "Magic Assistant" cmd /k "python tools\assistant\main.py"
timeout /t 1 /nobreak >nul

echo.
echo   ================================================
echo    Services live:
echo.
echo    Dashboard:     http://localhost:8000
echo    Daemon API:    http://localhost:8001
echo    AI Assistant:  http://localhost:8300
echo    Messenger:     http://localhost:8400
echo    MQTT:          localhost:1883
echo   ================================================
echo.
pause
