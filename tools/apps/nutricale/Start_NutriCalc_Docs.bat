@echo off
TITLE NutriCalc Documentation Server
color 0B
echo =======================================================
echo       NutriCalc Documentation Server
echo =======================================================
echo.
echo Starting documentation server on port 8101...
echo Documentation will be accessible at: http://localhost:8101/
echo Leave this window open to keep the server running.
echo Press Ctrl+C to stop the server.
echo.
:loop
cd /d "%~dp0\..\tools\nutribuddy"
python -m http.server 8101 -d docs
if errorlevel 1 (
    color 0C
    echo.
    echo [ERROR] Documentation server crashed or failed to start.
    echo Attempting automatic restart in 5 seconds...
    timeout /t 5 /nobreak >nul
    color 0B
    goto loop
)
