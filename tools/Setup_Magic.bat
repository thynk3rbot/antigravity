@echo off
TITLE Magic Bridge Setup
echo =======================================================
echo          Magic Magic Bridge Setup
echo =======================================================
echo.
echo Installing Python dependencies for Magic...
pip install -r "%~dp0magic\requirements.txt"
echo.
echo Setup complete. You can now use Start_All_Services.bat
pause
