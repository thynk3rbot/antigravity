@echo off
TITLE LoRaLink Master Control
color 0D
echo =======================================================
echo          LoRaLink All Services Starter
echo =======================================================
echo.
echo Launching MQTT Broker (EMQX Docker - Ports 1883 / 8083)...
start "LoRaLink MQTT Broker" cmd /c "%~dp0Start_MQTT_Server.bat"
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
echo All services launched in separate windows!
echo.
echo   MQTT Broker:    localhost:1883  (TCP)
echo   MQTT WebSocket: localhost:8083
echo   EMQX Dashboard: http://localhost:18083  (admin / public)
echo   Docs:           http://localhost:8001
echo   Controller:     http://localhost:8000
echo   Website:        http://localhost:8010
echo.
echo You can now close this supervisor window.
pause
