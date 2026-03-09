@echo off
TITLE LoRaLink Public Demo Tunnels
color 0E
echo =======================================================
echo          LoRaLink Public Demo Tunnel Setup
echo =======================================================
echo.
echo This script will create temporary public URLs for your
echo services using 'localtunnel'.
echo NO ROUTER OR DNS CHANGES REQUIRED.
echo.

REM Ensure local services are running first
echo [1/4] Ensuring local services are running...
start "LoRaLink Services" cmd /c "%~dp0Start_All_Services.bat"
timeout /t 5 /nobreak > nul

echo.
echo [2/4] Starting Public Tunnel for FLEET APP (Port 8000)...
start "Tunnel: App" cmd /k "npx localtunnel --port 8000"

echo.
echo [3/4] Starting Public Tunnel for DOCS (Port 8001)...
start "Tunnel: Docs" cmd /k "npx localtunnel --port 8001"

echo.
echo [4/4] Starting Public Tunnel for WEBSITE (Port 8010)...
start "Tunnel: Website" cmd /k "npx localtunnel --port 8010"

echo.
echo =======================================================
echo ALL TUNNELS STARTING!
echo =======================================================
echo.
echo 1. Look for the 'url:' lines in the new windows.
echo 2. When you visit the URL, you might be asked for an
echo    'Endpoint IP'. This is YOUR home public IP.
echo.
echo Keep all windows open to maintain public access.
echo.
pause
