@echo off
TITLE LoRaLink Master Control
color 0D
echo =======================================================
echo          LoRaLink All Services Starter
echo =======================================================
echo.

:: Ensure Docker is ready before starting anything
docker stats --no-stream >nul 2>&1
if %errorlevel% neq 0 (
    echo [1/3] Starting Docker Desktop...
    start "" "C:\Program Files\Docker\Docker\Docker Desktop.exe"
    echo [2/3] Waiting for Docker Engine to be ready...
    :waitLoop
    docker stats --no-stream >nul 2>&1
    if %errorlevel% neq 0 (
        timeout /t 2 /nobreak >nul
        goto waitLoop
    )
    echo [3/3] Docker is READY. Proceeding with Loralink services...
    echo.
)
echo Launching MQTT Broker (EMQX Docker - Ports 1883 / 8083)...
start "LoRaLink MQTT Broker" cmd /c "%~dp0Start_MQTT_Server.bat"
echo.
echo Launching RAG Router (Port 8200)...
start "LoRaLink RAG Router" cmd /c "%~dp0Start_RAG_Router.bat"
echo.
echo Launching Documentation Server (Port 8001)...
start "LoRaLink Docs Server" cmd /c "%~dp0Start_Docs_Server.bat"
echo.
echo Launching Fleet Administrator (Port 8000)...
start "LoRaLink Fleet Admin" cmd /c "%~dp0Start_Fleet_Admin.bat"
echo.
echo Launching Corporate Website (Port 8010)...
start "LoRaLink Website" cmd /c "%~dp0Start_Website.bat"
echo.
echo Launching Local AI Workstation (Port 3000)...
start "AI Workstation" cmd /c "%~dp0Start_AI_Workstation.bat"
echo.
echo Launching Magic Control Interface (Port 8500)...
start "Magic Control" cmd /c "cd %~dp0magic && python bridge.py"
echo.
echo All services launched in separate windows!
echo.
echo   MQTT Broker:    localhost:1883  (TCP)
echo   MQTT WebSocket: localhost:8083
echo   EMQX Dashboard: http://localhost:18083  (admin / public)
echo   Docs:           http://localhost:8001
echo   Controller:     http://localhost:8000
echo   RAG Router:     http://localhost:8200
echo   Website:        http://localhost:8010
echo   AI WebUI:       http://localhost:3000
echo   Magic Control:  http://localhost:8500
echo.
echo You can now close this supervisor window.
pause
