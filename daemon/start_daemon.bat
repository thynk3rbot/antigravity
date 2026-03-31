@echo off
TITLE Magic Daemon
color 0D
cls

echo.
echo   ================================================
echo    Magic Daemon  —  Fleet Orchestrator
echo   ================================================
echo.

cd /d "%~dp0.."

:: Verify Python
python --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python not found in PATH.
    pause
    exit /b 1
)

:: Install deps if needed
pip show fastapi >nul 2>&1
if errorlevel 1 (
    echo Installing requirements...
    pip install -r daemon\requirements.txt
    if errorlevel 1 (
        echo [ERROR] Failed to install requirements.
        pause
        exit /b 1
    )
)

:: Allow overrides: start_daemon.bat [port] [mqtt_broker] [log_level]
set PORT=8001
set MQTT=localhost:1883
set LOG=INFO

if not "%~1"=="" set PORT=%~1
if not "%~2"=="" set MQTT=%~2
if not "%~3"=="" set LOG=%~3

echo    API Port:    %PORT%
echo    MQTT:        %MQTT%
echo    Log Level:   %LOG%
echo.
echo    Dashboard:   http://localhost:8000
echo    API:         http://localhost:%PORT%
echo.
echo Starting...
echo.

:loop
python daemon\src\main.py --port %PORT% --mqtt-broker %MQTT% --log-level %LOG%

echo.
echo [!] Daemon stopped. Restarting in 5s...
timeout /t 5 /nobreak >nul
goto loop
