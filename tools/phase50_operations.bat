@echo off
REM Phase 50 Operations Control Panel
REM Unified management for fleet test + Ollama code generation + hybrid proxy
REM
REM This is your command center for Phase 50:
REM   - Fleet test monitoring (daemon logs)
REM   - Ollama task queue (Phase 50.2 code generation)
REM   - Hybrid proxy (local + cloud model routing)
REM   - Cost tracking and metrics

setlocal enabledelayedexpansion

if "%~1"=="dashboard" goto :dashboard
if "%~1"=="start" goto :start_all
if "%~1"=="stop" goto :stop_all
if "%~1"=="status" goto :status
if "%~1"=="queue" goto :queue_phase50_2
if "%~1"=="monitor" goto :monitor
if "%~1"=="logs" goto :show_logs
if "%~1"=="" goto :menu

:menu
cls
echo.
echo ============================================
echo PHASE 50 OPERATIONS CONTROL PANEL
echo ============================================
echo.
echo Fleet Test Status: [Check status with 'phase50_operations.bat status']
echo.
echo Commands:
echo   phase50_operations.bat start          - Start all services
echo   phase50_operations.bat stop           - Stop all services
echo   phase50_operations.bat status         - Check all service status
echo   phase50_operations.bat dashboard      - Live monitoring dashboard
echo   phase50_operations.bat queue          - Queue Phase 50.2 Ollama tasks
echo   phase50_operations.bat monitor        - Monitor fleet test results
echo   phase50_operations.bat logs           - Show daemon logs
echo.
echo Available Services:
echo   1. Daemon (fleet test)
echo   2. Ollama (code generation)
echo   3. Hybrid Proxy (model routing)
echo.
pause
exit /b 0

:start_all
echo.
echo ============================================
echo Starting Phase 50 Services
echo ============================================
echo.

echo [1/3] Starting Hybrid Model Proxy...
call "%~dp0hybrid_proxy.bat" start
echo.

echo [2/3] Checking Ollama...
tasklist /FI "IMAGENAME eq ollama.exe" 2>NUL | find /I /N "ollama" >NUL
if "%ERRORLEVEL%"=="0" (
    echo [OK] Ollama already running
) else (
    echo [!] Ollama not running - start it manually or via Ollama app
)
echo.

echo [3/3] Services status:
call "%~dp0hybrid_proxy.bat" status
echo.

echo [+] Phase 50 services ready!
echo.
pause
exit /b 0

:stop_all
echo.
echo ============================================
echo Stopping Phase 50 Services
echo ============================================
echo.

call "%~dp0hybrid_proxy.bat" stop
echo.
echo [+] Services stopped
pause
exit /b 0

:status
cls
echo.
echo ============================================
echo PHASE 50 SERVICES STATUS
echo ============================================
echo.

echo [HYBRID PROXY]
call "%~dp0hybrid_proxy.bat" status
echo.

echo [OLLAMA QUEUE]
call "%~dp0ollama_queue.bat" check
echo.

echo [DAEMON]
if exist "test_daemon.log" (
    echo Last 5 registrations:
    findstr /C:"[Peer]" test_daemon.log | tail -5
    echo.
    echo Last command:
    findstr /C:"[Command]" test_daemon.log | tail -1
) else (
    echo Daemon not running (test_daemon.log not found)
)
echo.
pause
exit /b 0

:dashboard
cls
:dashboard_loop
cls
echo.
echo ============================================
echo PHASE 50 LIVE DASHBOARD
echo ============================================
echo Updated: %date% %time%
echo.

echo [FLEET TEST - DEVICE REGISTRATIONS]
if exist "test_daemon.log" (
    findstr /C:"[Peer]" test_daemon.log | tail -10
) else (
    echo (daemon not running)
)
echo.

echo [OLLAMA QUEUE]
call "%~dp0ollama_queue.bat" check | head -5
echo.

echo [HYBRID PROXY - RECENT REQUESTS]
if exist "%APPDATA%\.claude\hybrid_proxy\*" (
    for /f %%F in ('dir /b /o-d "%APPDATA%\.claude\hybrid_proxy\proxy_*.log" 2^>NUL ^| head -1') do (
        findstr "backend" "%APPDATA%\.claude\hybrid_proxy\%%F" 2>NUL | tail -5
    )
) else (
    echo (no requests yet)
)
echo.

echo Press Ctrl+C to exit, or wait for next refresh...
timeout /t 10 /nobreak
goto :dashboard_loop

:queue_phase50_2
echo.
echo ============================================
echo Queue Phase 50.2 Ollama Tasks
echo ============================================
echo.
echo This will queue the master Phase 50.2 task to Ollama
echo (Master prompt includes full specification and context)
echo.
echo Requirements:
echo   - Ollama app must be running
echo   - OpenRouter API key optional (for fallback)
echo.
pause

call "%~dp0queue_phase50_2_master.bat"
echo.
echo [+] Task queued! Monitor with: ollama_queue.bat check
pause
exit /b 0

:monitor
echo.
echo ============================================
echo Fleet Test Monitoring
echo ============================================
echo.

call "%~dp0monitor_phase50.bat" fleet
exit /b 0

:show_logs
echo.
echo ============================================
echo Daemon Logs (Streaming)
echo ============================================
echo.
echo Ctrl+C to stop
echo.

if exist "test_daemon.log" (
    powershell -Command "Get-Content -Path 'test_daemon.log' -Tail 50 -Wait"
) else (
    echo test_daemon.log not found
    echo Make sure daemon is running in another terminal
    pause
)
exit /b 0
