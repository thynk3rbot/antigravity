@echo off
TITLE LoRaLink Ecosystem Launch Control
color 0B

echo =======================================================
echo          LoRaLink Ecosystem Launch Control
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
    echo [3/3] Docker is READY. Proceeding with LoRaLink services...
    echo.
)

:: Use 'call' to run the master services script
:: %~dp0 is the directory containing this batch file
call "%~dp0Start_All_Services.bat"

echo.
echo =====================================================
echo LoRaLink Stack is starting!
echo Dashboard: http://localhost:8000
echo Website:   http://localhost:8010
echo Magic App: http://localhost:8500
echo =====================================================
echo.
:: Only pause if the script was launched manually (explaining why wait)
echo Starting services... this window will stay open for logs.
timeout /t 60
exit
