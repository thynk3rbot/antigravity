@echo off
TITLE NutriCalc - Complete Development Environment
color 0E
echo =======================================================
echo    NutriCalc - Complete Development Environment
echo =======================================================
echo.
echo Launching NutriCalc services...
echo   1. App server     → http://localhost:8100/static/
echo   2. Docs server    → http://localhost:8101/
echo.
echo Starting services in separate windows...
echo Leave all windows open to keep services running.
echo.

REM Get the directory of this script
cd /d "%~dp0"

REM Start NutriCalc App Server
echo [*] Starting NutriCalc App Server (port 8100)...
start "NutriCalc App" cmd /k "Start_NutriCalc_App.bat"
timeout /t 2 /nobreak >nul

REM Start NutriCalc Documentation Server
echo [*] Starting NutriCalc Documentation Server (port 8101)...
start "NutriCalc Docs" cmd /k "Start_NutriCalc_Docs.bat"
timeout /t 2 /nobreak >nul

REM Display summary
color 0B
echo.
echo =======================================================
echo    NutriCalc Services Started Successfully
echo =======================================================
echo.
echo Available URLs:
echo   App:       http://localhost:8100/static/
echo   Docs:      http://localhost:8101/
echo   Combined:  http://localhost:8100/
echo.
echo To test:
echo   1. Open http://localhost:8100/static/ in your browser
echo   2. Select chemicals and click "Solve Formula"
echo   3. View production workflow and mixing guide
echo   4. Open http://localhost:8101/ for documentation
echo.
echo To stop services:
echo   - Close the server windows
echo   - Or press Ctrl+C in each window
echo.
echo For more information, see:
echo   - README.md in tools/nutribuddy/
echo   - DEPLOY.md for deployment guide
echo.
pause
