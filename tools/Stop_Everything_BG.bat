@echo off
echo Stopping Background Services...
cd /d "%~dp0"
if exist "bg_services.pid" (
    set /p PID=<bg_services.pid
    taskkill /PID %PID% /T /F >nul 2>&1
    del bg_services.pid
    echo Stopped supervisor and child processes!
) else (
    echo background processes pid not found, cleaning up stray python tasks manually is needed.
)

:: Wait a moment for processes to close
timeout /t 2 >nul
echo Done.
