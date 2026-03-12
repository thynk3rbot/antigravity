# LoRaLink Dual Deployment Script
Write-Host "--- Starting LoRaLink Dual Deployment ---" -ForegroundColor Cyan

$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
$master_ip = "172.16.0.27"
$slave_ip = "172.16.0.26"

Write-Host "Deplopying to Master ($master_ip)..." -ForegroundColor Yellow
& $pio run --environment ota_master --target upload

if ($LASTEXITCODE -eq 0) {
    Write-Host "Master deployment SUCCESS." -ForegroundColor Green
} else {
    Write-Host "Master deployment FAILED." -ForegroundColor Red
}

Write-Host "Deploying to Slave ($slave_ip)..." -ForegroundColor Yellow
& $pio run --environment ota_slave --target upload

if ($LASTEXITCODE -eq 0) {
    Write-Host "Slave deployment SUCCESS." -ForegroundColor Green
} else {
    Write-Host "Slave deployment FAILED." -ForegroundColor Red
}

Write-Host "--- Deployment Cycle Complete ---" -ForegroundColor Cyan
