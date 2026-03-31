@echo off
REM Monitor Phase 50 Fleet Test + Phase 50.2 Ollama Progress
REM
REM Usage:
REM   monitor_phase50.bat fleet    - Monitor fleet test daemon logs
REM   monitor_phase50.bat ollama   - Check Ollama queue + results
REM   monitor_phase50.bat both     - Monitor everything (default)

setlocal enabledelayedexpansion

if "%~1"=="fleet" goto :fleet
if "%~1"=="ollama" goto :ollama
if "%~1"=="" goto :both
if "%~1"=="both" goto :both

echo Usage: monitor_phase50.bat [fleet^|ollama^|both]
exit /b 1

:both
cls
echo.
echo ============================================
echo PHASE 50 FLEET TEST + PHASE 50.2 MONITOR
echo ============================================
echo.
echo [FLEET TEST - DAEMON LOGS]
echo.
if exist "test_daemon.log" (
    echo === LATEST REGISTRATIONS ===
    findstr /C:"[Peer]" test_daemon.log | tail -10
    echo.
    echo === LATEST COMMANDS ===
    findstr /C:"[Command]" test_daemon.log | tail -5
    echo.
    echo === LATEST ACKS ===
    findstr /C:"[Ack]" test_daemon.log | tail -5
    echo.
    echo === ERRORS (if any) ===
    findstr /I "[error]" test_daemon.log | tail -3
) else (
    echo ERROR: test_daemon.log not found - daemon may not be running
)

echo.
echo [OLLAMA - QUEUE STATUS]
echo.
call ollama_queue.bat check
echo.
echo === QUEUE FILE STATUS ===
if exist "%APPDATA%\Magic\ollama_queue.txt" (
    echo Queue exists, tasks:
    findstr "TASK_ID=" "%APPDATA%\Magic\ollama_queue.txt" | wc -l
) else (
    echo No queue file yet
)

echo.
echo [REFRESH STATS]
echo Updated: %date% %time%
echo Press Ctrl+C to exit
timeout /t 10 /nobreak
goto :both

:fleet
cls
echo.
echo ============================================
echo FLEET TEST MONITORING
echo ============================================
echo.
echo === DEVICE REGISTRATIONS ===
if exist "test_daemon.log" (
    findstr /C:"[Peer]" test_daemon.log
) else (
    echo ERROR: test_daemon.log not found
)

echo.
echo === COMMAND EXECUTIONS ===
if exist "test_daemon.log" (
    findstr /C:"[Command]" test_daemon.log
) else (
    echo ERROR: test_daemon.log not found
)

echo.
echo === ACK RECEIPTS ===
if exist "test_daemon.log" (
    findstr /C:"[Ack]" test_daemon.log
) else (
    echo ERROR: test_daemon.log not found
)

echo.
echo === ERROR LOG ===
if exist "test_daemon.log" (
    findstr /I "[error]" test_daemon.log
) else (
    echo ERROR: test_daemon.log not found
)

echo.
timeout /t 5 /nobreak
goto :fleet

:ollama
echo.
echo ============================================
echo OLLAMA QUEUE MONITORING
echo ============================================
echo.
echo === QUEUED TASKS ===
call ollama_queue.bat check

echo.
echo === QUEUE FILE ===
if exist "%APPDATA%\Magic\ollama_queue.txt" (
    type "%APPDATA%\Magic\ollama_queue.txt"
) else (
    echo No queue file yet
)

echo.
echo === RESULTS STATUS ===
if exist "%APPDATA%\Magic\ollama_results" (
    dir /b "%APPDATA%\Magic\ollama_results"
) else (
    echo No results directory yet
)

echo.
timeout /t 5 /nobreak
goto :ollama
