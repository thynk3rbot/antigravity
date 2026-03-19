@echo off
TITLE LoRaLink AI Mesh Daemon
color 0E

echo =======================================================
echo          LoRaLink AI Mesh Daemon (Ollama Bridge)
echo =======================================================
echo.

:: Detect COM port - suggest COM3 as default if not provided
set PORT=%1
if "%PORT%"=="" set PORT=COM3

:: Detect Model - use qwen2.5-coder:14b as default
set MODEL=%2
if "%MODEL%"=="" set MODEL=qwen2.5-coder:14b

echo [*] Starting bridge on %PORT% using %MODEL%...
echo [*] Ensure Ollama is running (http://localhost:11434)
echo.

python "%~dp0pc-daemon.py" --port %PORT% --model %MODEL%

if %errorlevel% neq 0 (
    echo.
    echo [!] Daemon exited with error. Check permissions or port availability.
    pause
)
