# magic-pulse.ps1 — Magic Fleet Bootstrapper
# This script manages the lifecycle of the Magic Sentinel infrastructure.

$MAGIC_ROOT = Get-Location
$LOG_DIR = Join-Path $MAGIC_ROOT "logs"
if (-not (Test-Path $LOG_DIR)) { New-Item -ItemType Directory -Path $LOG_DIR }

Write-Host "--- Magic Pulse Bootstrapper ---" -ForegroundColor Cyan

# 1. Check for Broker (Mosquitto)
Write-Host "[1/3] Checking for Magic Bus (MQTT)..." -NoNewline
$broker = Get-Process mosquitto -ErrorAction SilentlyContinue
if ($null -eq $broker) {
    Write-Host " OFFLINE" -ForegroundColor Red
    Write-Host "      [!] Please ensure Mosquitto is installed and running locally." -ForegroundColor Yellow
    exit
}
Write-Host " ONLINE (PID: $($broker.Id))" -ForegroundColor Green

# 2. Start Magic LVC (Cache)
Write-Host "[2/3] Starting Magic LVC (SQLite)..." -NoNewline
$lvc = Start-Process python -ArgumentList 'daemon/src/lvc_service.py' -PassThru -WindowStyle Hidden
if ($null -eq $lvc) {
    Write-Host " FAILED" -ForegroundColor Red
} else {
    Write-Host " ACTIVE (PID: $($lvc.Id))" -ForegroundColor Green
}

# 3. Start Sentinel Bridge
Write-Host "[3/3] Starting Sentinel Bridge (Remote T-Beam)..." -NoNewline
# Pull host from config.json
$config = Get-Content "daemon/config.json" | ConvertFrom-Json
$host_ip = $config.services | Where-Object { $_.name -eq "meshtastic_bridge" } | Select-Object -ExpandProperty meshtastic_host

$bridge = Start-Process python -ArgumentList "daemon/src/meshtastic_bridge.py --host $host_ip" -PassThru -WindowStyle Hidden
if ($null -eq $bridge) {
    Write-Host " FAILED" -ForegroundColor Red
} else {
    Write-Host " ACTIVE (PID: $($bridge.Id))" -ForegroundColor Green
}

Write-Host "`n--- Sentinel Pulse is LIVE ---" -ForegroundColor Cyan
Write-Host "Logs: ./logs/magic.log"
Write-Host "REST API: http://localhost:8200/tables"
