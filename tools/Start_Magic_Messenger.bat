@echo off
TITLE Magic Messenger
color 0D
cls

echo.
echo   ================================================
echo    Magic Messenger  —  Mesh Chat Bridge
echo   ================================================
echo.
echo    WebSocket:  ws://localhost:8400/ws
echo    PWA UI:     http://localhost:8400
echo    Install as app on phone via browser menu
echo.

cd /d "%~dp0.."

:loop
python tools\loramsg\server.py

echo.
echo [!] Messenger stopped. Restarting in 3s...
timeout /t 3 /nobreak >nul
goto loop
