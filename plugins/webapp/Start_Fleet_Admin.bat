@echo off
TITLE Magic Fleet Administrator
color 0A
echo =======================================================
echo          Magic Fleet Administrator
echo =======================================================
echo.
echo Starting services...
echo Fleet Admin UI: http://localhost:8000
echo Daemon API:     http://localhost:8001/docs
echo.

:: Run from repo root so module paths resolve
cd /d "%~dp0\.."

:: Start daemon in minimized window
start "Magic Daemon :8001" /min cmd /k "python -m tools.daemon.daemon"

:: Start webapp in minimized window
start "Magic Webapp :8000" /min cmd /k "cd tools\webapp && python server.py"

:: Wait for services to start, then launch tray icon
timeout /t 3 /nobreak >nul
echo Launching system tray icon...
pythonw -m tools.daemon.tray
