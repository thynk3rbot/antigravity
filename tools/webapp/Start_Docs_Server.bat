@echo off
TITLE LoRaLink Documentation Server
color 0B
echo =======================================================
echo          LoRaLink Documentation Server Setup
echo =======================================================
echo.
echo Starting local web server for documentation on port 8001...
echo Leave this window open to keep the server running.
echo To access the documentation, go to http://localhost:8001 in your browser.
echo.
cd /d "%~dp0\..\.."
py -m http.server 8001 -d docs
if errorlevel 1 (
    color 0C
    echo.
    echo [ERROR] Failed to start Python HTTP server. Make sure Python is installed.
    pause
)
