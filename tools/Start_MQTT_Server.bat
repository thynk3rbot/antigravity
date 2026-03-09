@echo off

cd /d "%~dp0\.."
docker compose -f mqttdocker.yml up
if errorlevel 1 (
    color 0C
    echo.
    echo [ERROR] Failed to start EMQX. Make sure Docker Desktop is running.
    pause
)

TITLE LoRaLink MQTT Broker (EMQX)
color 09
echo =======================================================
echo          LoRaLink MQTT Broker (EMQX via Docker)
echo =======================================================
echo.
echo Starting EMQX MQTT broker...
echo   MQTT TCP:       localhost:1883
echo   MQTT WebSocket: localhost:8083
echo   EMQX Dashboard: http://localhost:18083  (admin / public)
echo.
echo Leave this window open to keep the broker running.
echo.
echo Checking if Docker Desktop is running...
docker stats --no-stream >nul 2>&1
if %errorlevel% neq 0 (
    echo Docker is not running. Starting Docker Desktop...
    start "" "C:\Program Files\Docker\Docker\Docker Desktop.exe"
    echo Waiting for Docker Engine to be fully ready...

    :waitLoop
    docker stats --no-stream >nul 2>&1
    if %errorlevel% neq 0 (
        timeout /t 2 /nobreak >nul
        goto waitLoop
    )
    echo Docker is now READY!
)

:: Start Redis for fast state storage
echo Start Redis for fast state storage
docker run -d --name redis-lora -p 6379:6379 redis:latest

:: Start Node-RED for logic/dashboard
echo Start Node-RED for logic/dashboard
docker run -d --name nodered-lora ^
  -p 1880:1880 ^
  -v node_red_user_data:/data ^
  nodered/node-red:latest


:: Add to your startup.bat
docker run -d --name emqx ^
  -p 1883:1883 ^
  -p 18083:18083 ^
  -v emqx-data:/opt/emqx/data ^
  emqx/emqx:latest


:: Insert the 'docker run' commands from above here
echo Stack is live!
echo Dashboard: http://localhost:18083
echo Node-RED: http://localhost:18080
pause

