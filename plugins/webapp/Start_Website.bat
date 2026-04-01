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
cd /d "%~dp0\..\website"
python server.py
if errorlevel 1 (
    color 0C
    echo.
    echo [ERROR] Failed to start the website server.
    echo Make sure dependencies are installed: pip install -r requirements.txt
    pause
)
