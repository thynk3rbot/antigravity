@echo off
TITLE Magic Startup Registration
color 0B

echo =======================================================
echo          Magic Startup Registration
echo =======================================================
echo.
echo This script will register Magic to start automatically
echo whenever you log into this computer.
echo.

:: Get absolute path to the Start_Everything.bat in the same directory
set "TARGET_PATH=%~dp0Start_Everything.bat"

if not exist "%TARGET_PATH%" (
    color 0C
    echo [ERROR] Could not find %TARGET_PATH%
    pause
    exit /b 1
)

echo Registering task "Magic_AutoStart"...
schtasks /create /tn "Magic_AutoStart" /tr "\"%TARGET_PATH%\"" /sc onlogon /rl highest /f

if %errorlevel% equ 0 (
    echo.
    echo =======================================================
    echo [SUCCESS] Magic is now set to start on login!
    echo =======================================================
    echo.
) else (
    color 0C
    echo.
    echo [ERROR] Failed to register task.
    echo Please try running this script as Administrator.
)

pause
