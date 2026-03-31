@echo off
TITLE Magic Fleet Administrator
color 0A
echo =======================================================
echo          Magic Fleet Administrator Backend
echo =======================================================
echo.
echo Starting PC backend server on port 8000...
echo Ensure your Heltec device is connected via USB for serial mapping if needed.
echo Leave this window open to keep the server running.
echo To access the Fleet Admin, go to http://localhost:8000 in your browser.
echo.
:loop
cd /d "%~dp0\webapp"
py server.py
if errorlevel 1 (
    color 0C
    echo.
    echo [ERROR] Fleet Administrator server crashed or failed to start.
    echo Attempting automatic restart in 5 seconds...
    timeout /t 5 /nobreak >nul
    color 0A
    goto loop
)
