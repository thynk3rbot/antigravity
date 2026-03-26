@echo off
REM Hybrid Model Proxy - Unified local Ollama + OpenRouter routing
REM
REM Usage:
REM   hybrid_proxy.bat start          - Start proxy server
REM   hybrid_proxy.bat stop           - Stop proxy server
REM   hybrid_proxy.bat status         - Check proxy status
REM   hybrid_proxy.bat test           - Run diagnostics
REM   hybrid_proxy.bat report         - Show cost/metrics report

setlocal enabledelayedexpansion

if "%~1"=="start" goto :start
if "%~1"=="stop" goto :stop
if "%~1"=="status" goto :status
if "%~1"=="test" goto :test
if "%~1"=="report" goto :report
if "%~1"=="" goto :help

:help
echo Hybrid Model Proxy Manager
echo.
echo Usage:
echo   hybrid_proxy.bat start          - Start proxy server (background)
echo   hybrid_proxy.bat stop           - Stop proxy server
echo   hybrid_proxy.bat status         - Check proxy health
echo   hybrid_proxy.bat test           - Run diagnostics
echo   hybrid_proxy.bat report         - Show metrics report
echo.
echo The proxy routes requests between:
echo   - Local Ollama (FREE, fast)
echo   - OpenRouter cloud (PAID, reliable fallback)
echo.
exit /b 1

:start
echo.
echo ============================================
echo Starting Hybrid Model Proxy
echo ============================================
echo.
echo Ollama endpoint: http://localhost:11434
echo OpenRouter endpoint: https://openrouter.ai/api/v1
echo.

REM Check if already running
tasklist /FI "WINDOWTITLE eq Hybrid Model Proxy*" 2>NUL | find /I /N "python" >NUL
if "%ERRORLEVEL%"=="0" (
    echo [!] Proxy already running
    exit /b 0
)

REM Start in new window with title
start "Hybrid Model Proxy" /MIN python "%~dp0hybrid_model_proxy.py"

echo [+] Proxy started in background
echo [+] Check status: hybrid_proxy.bat status
exit /b 0

:stop
echo.
echo ============================================
echo Stopping Hybrid Model Proxy
echo ============================================
echo.

tasklist /FI "WINDOWTITLE eq Hybrid Model Proxy*" 2>NUL | find /I /N "python" >NUL
if "%ERRORLEVEL%"=="0" (
    taskkill /FI "WINDOWTITLE eq Hybrid Model Proxy*" /T /F
    echo [+] Proxy stopped
) else (
    echo [-] Proxy not running
)

exit /b 0

:status
echo.
echo ============================================
echo Hybrid Model Proxy Status
echo ============================================
echo.

tasklist /FI "WINDOWTITLE eq Hybrid Model Proxy*" 2>NUL | find /I /N "python" >NUL
if "%ERRORLEVEL%"=="0" (
    echo [OK] Proxy is RUNNING
    echo.
    echo Checking backends...
) else (
    echo [!] Proxy is NOT RUNNING
    echo.
    echo To start: hybrid_proxy.bat start
    exit /b 1
)

REM Quick health check
echo Testing Ollama...
powershell -Command "try { $r = Invoke-RestMethod -Uri 'http://localhost:11434/api/tags' -TimeoutSec 5; Write-Host '[OK] Ollama responding' } catch { Write-Host '[-] Ollama not responding' }"

echo Testing OpenRouter...
powershell -Command "try { $r = Invoke-RestMethod -Uri 'https://openrouter.ai/api/v1/models' -TimeoutSec 5 -Headers @{'Authorization'='Bearer dummy'}; Write-Host '[OK] OpenRouter API accessible' } catch { Write-Host '[-] OpenRouter API error' }"

echo.
exit /b 0

:test
echo.
echo ============================================
echo Hybrid Model Proxy - Diagnostics
echo ============================================
echo.
echo Running test queries...
echo.

python "%~dp0hybrid_model_proxy.py" 2>&1 | find /V "Traceback" | find /V "UnicodeEncodeError"

echo.
echo [+] Diagnostics complete
echo [+] Check logs: %APPDATA%\.claude\hybrid_proxy\
exit /b 0

:report
echo.
echo ============================================
echo Hybrid Model Proxy - Metrics Report
echo ============================================
echo.

REM Parse logs and generate report
if exist "%APPDATA%\.claude\hybrid_proxy\*" (
    echo Searching for metrics...
    for /f %%F in ('dir /b /o-d "%APPDATA%\.claude\hybrid_proxy\proxy_*.log" 2^>NUL') do (
        set "LATEST=%%F"
        goto :show_report
    )
    :show_report
    if defined LATEST (
        echo Latest log: !LATEST!
        echo.
        echo === Ollama Requests ===
        findstr /C:"backend.*ollama" "%APPDATA%\.claude\hybrid_proxy\!LATEST!" 2>NUL | find /C /V "" || echo (none)
        echo.
        echo === OpenRouter Requests ===
        findstr /C:"backend.*openrouter" "%APPDATA%\.claude\hybrid_proxy\!LATEST!" 2>NUL | find /C /V "" || echo (none)
        echo.
        echo === Total Cost ===
        findstr /C:"cost_usd" "%APPDATA%\.claude\hybrid_proxy\!LATEST!" 2>NUL || echo (no data)
    )
) else (
    echo No proxy logs found yet
    echo Logs stored in: %APPDATA%\.claude\hybrid_proxy\
)

echo.
exit /b 0
