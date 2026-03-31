@echo off
TITLE NutriCalc - Hydroponic Nutrient Formula Solver
color 0A
echo =======================================================
echo       NutriCalc - Local Development Server
echo =======================================================
echo.
echo Starting NutriCalc app on port 8100...
echo App will be accessible at: http://localhost:8100/static/
echo Documentation: http://localhost:8100/docs/
echo.
echo Leave this window open to keep the server running.
echo Press Ctrl+C to stop the server.
echo.
:loop
cd /d "%~dp0\..\tools\nutribuddy"
python server.py
if errorlevel 1 (
    color 0C
    echo.
    echo [ERROR] NutriCalc server crashed or failed to start.
    echo Attempting automatic restart in 5 seconds...
    timeout /t 5 /nobreak >nul
    color 0A
    goto loop
)
