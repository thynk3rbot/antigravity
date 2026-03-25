@echo off
TITLE LoRaLink Fleet Administrator
color 0A
echo =======================================================
echo          LoRaLink Fleet Administrator
echo =======================================================
echo.
echo Starting Daemon (port 8001) and Webapp (port 8000)...
echo Fleet Admin UI: http://localhost:8000
echo Daemon API:     http://localhost:8001
echo.
cd /d "%~dp0\.."

:: Start daemon in background window
start "LoRaLink Daemon :8001" cmd /k "python -m tools.daemon.daemon --config tools/daemon/daemon.config.json"

:: Brief pause to let daemon initialize
timeout /t 2 /nobreak >nul

:: Start webapp in this window
cd /d "%~dp0"
python server.py
if errorlevel 1 (
    color 0C
    echo.
    echo [ERROR] Failed to start the Fleet Administrator server.
    pause
)
