@echo off
REM Phase 50 Daemon Startup Script
REM Starts the Magic Daemon with mesh sovereignty capabilities

setlocal enabledelayedexpansion

echo ================================================================
echo Magic Phase 50 Daemon
echo ================================================================
echo.

REM Check if Python is available
python --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python not found in PATH
    exit /b 1
)

REM Check if requirements are installed
echo Checking dependencies...
pip show fastapi >nul 2>&1
if errorlevel 1 (
    echo Installing requirements...
    pip install -r requirements.txt
    if errorlevel 1 (
        echo ERROR: Failed to install requirements
        exit /b 1
    )
)

REM Parse arguments
set PORT=8001
set MQTT_BROKER=localhost:1883
set LOG_LEVEL=INFO

if not "%~1"=="" set PORT=%~1
if not "%~2"=="" set MQTT_BROKER=%~2
if not "%~3"=="" set LOG_LEVEL=%~3

echo.
echo Configuration:
echo   API Port:      %PORT%
echo   MQTT Broker:   %MQTT_BROKER%
echo   Log Level:     %LOG_LEVEL%
echo.
echo Starting daemon...
echo.

REM Start daemon
python src/main.py --port %PORT% --mqtt-broker %MQTT_BROKER% --log-level %LOG_LEVEL%

if errorlevel 1 (
    echo.
    echo ERROR: Daemon failed to start
    exit /b 1
)
