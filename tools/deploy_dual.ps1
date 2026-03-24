# LoRaLink Fleet Deployment Script
Write-Host "--- Starting LoRaLink Fleet Deployment ---" -ForegroundColor Cyan

# Gracefully terminate the WebApp backend to release all USB COM ports
Write-Host "Releasing COM Ports..." -ForegroundColor DarkGray
Get-WmiObject Win32_Process | Where-Object { $_.CommandLine -match "server\.py" } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 1

$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"

# 1. Flash V3 units (IPs 26, 27)
Write-Host "Deploying V3 to Master (172.16.0.27)..." -ForegroundColor Yellow
& $pio run -d firmware/v2 --environment heltec_v3_ota_27 --target upload

Write-Host "Deploying V3 to Slave (172.16.0.26)..." -ForegroundColor Yellow
& $pio run -d firmware/v2 --environment heltec_v3_ota_26 --target upload

# 2. Flash V4c units (IPs 28, 29)
Write-Host "Deploying V4 to Slave (172.16.0.28)..." -ForegroundColor Yellow
& $pio run -d firmware/v2 --environment heltec_v4_ota_28 --target upload

Write-Host "Deploying V4 to Slave (172.16.0.29)..." -ForegroundColor Yellow
& $pio run -d firmware/v2 --environment heltec_v4_ota_29 --target upload

Write-Host "Deploying V2/Webserver to Node 30 (via USB auto-detect)..." -ForegroundColor Yellow
& $pio run -d firmware/v2 --environment heltec_v2 --target upload

Write-Host "--- Deployment Cycle Complete ---" -ForegroundColor Cyan
