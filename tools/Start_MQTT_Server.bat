@echo off
TITLE Magic MQTT Broker
color 09
cls

echo.
echo   ================================================
echo    Magic MQTT Broker  —  mosquitto
echo   ================================================
echo.
echo    TCP:       localhost:1883
echo    WebSocket: localhost:9001  (if enabled in conf)
echo.

cd /d "%~dp0.."

:: Check mosquitto is installed
where mosquitto >nul 2>&1
if errorlevel 1 (
    echo [ERROR] mosquitto not found in PATH.
    echo         Install: winget install mosquitto
    echo         Or download: https://mosquitto.org/download/
    echo.
    pause
    exit /b 1
)

echo Starting mosquitto with mosquitto.conf...
echo.
mosquitto -v -c mosquitto.conf

if errorlevel 1 (
    echo.
    echo [ERROR] MQTT broker exited unexpectedly.
    pause
)
