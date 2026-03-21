@echo off
TITLE LoRaLink Fleet Administrator
color 0A
echo =======================================================
echo          LoRaLink Fleet Administrator Backend
echo =======================================================
echo.
echo Starting PC backend server on port 8000...
echo Ensure your Heltec device is connected via USB for serial mapping if needed.
echo Leave this window open to keep the server running.
echo To access the Fleet Admin, go to http://localhost:8000 in your browser.
echo.
cd /d "%~dp0"
py server.py
if errorlevel 1 (
    color 0C
    echo.
    echo [ERROR] Failed to start the Fleet Administrator server.
    pause
)
