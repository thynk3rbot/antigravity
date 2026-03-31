@echo off
TITLE Magic Public Demo Tunnels (Cloudflare)
color 0B
echo =======================================================
echo          Magic Public Demo (Cloudflare)
echo =======================================================
echo.
echo This script will create temporary public URLs using
echo Cloudflare Quick Tunnels.
echo NO ACCOUNT, ROUTER, OR DNS CHANGES REQUIRED!
echo.

REM Ensure local services are running first
echo [1/4] Ensuring local services are running...
start "Magic Services" cmd /c "%~dp0Start_All_Services.bat"
timeout /t 5 /nobreak > nul

echo.
echo [2/4] Starting Cloudflare Quick Tunnel for APP (8000)...
start "Cloudflare: App" cmd /k "cloudflared tunnel --url http://localhost:8000"

echo.
echo [3/4] Starting Cloudflare Quick Tunnel for DOCS (8001)...
start "Cloudflare: Docs" cmd /k "cloudflared tunnel --url http://localhost:8001"

echo.
echo [4/4] Starting Cloudflare Quick Tunnel for WEBSITE (8010)...
start "Cloudflare: Website" cmd /k "cloudflared tunnel --url http://localhost:8010"

echo.
echo =======================================================
echo ALL QUICK TUNNELS STARTING!
echo =======================================================
echo.
echo 1. Wait for the 'https://*.trycloudflare.com' URLs.
echo 2. Copy and send them to anyone!
echo.
echo Keep all windows open to maintain public access.
echo.
pause
