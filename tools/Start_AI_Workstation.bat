@echo off
TITLE Local AI Workstation
color 0A
echo =======================================================
echo          Magic Local AI Workstation Initializer
echo =======================================================
echo.
echo Launching workstation setup...
py "%~dp0..\setup_workstation.py"
echo.
echo Setup Complete. Local UI running at http://localhost:3000
echo You can safely close this window.
pause
