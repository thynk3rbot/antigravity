@echo off
TITLE Magic Corporate Website
color 0E
echo =======================================================
echo          Magic Corporate Website
echo =======================================================
echo.
echo Starting corporate website on port 8010...
echo Leave this window open to keep the server running.
echo To access the website, go to http://localhost:8010 in your browser.
echo.
:loop
cd /d "%~dp0\website"
py server.py
if errorlevel 1 (
    color 0C
    echo.
    echo [ERROR] Corporate website server crashed or failed to start.
    echo Attempting automatic restart in 5 seconds...
    timeout /t 5 /nobreak >nul
    color 0E
    goto loop
)
