@echo off
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
docker info >nul 2>&1
if %errorlevel% neq 0 (
    echo Docker is not running. Attempting to start Docker Desktop...
    start "" "C:\Program Files\Docker\Docker\Docker Desktop.exe"
    echo Waiting for Docker to initialize (this may take a minute)...

    :waitForDocker
    timeout /t 5 /nobreak >nul
    docker info >nul 2>&1
    if %errorlevel% neq 0 (
        echo Still waiting for Docker...
        goto waitForDocker
    )
    echo Docker is now running!
)

cd /d "%~dp0\.."
docker compose -f mqttdocker.yml up
if errorlevel 1 (
    color 0C
    echo.
    echo [ERROR] Failed to start EMQX. Make sure Docker Desktop is running.
    pause
)
