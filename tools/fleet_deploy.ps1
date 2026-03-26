# LoRaLink Fleet Deploy Script
# Build once, push to any device by IP.
# Usage: .\tools\fleet_deploy.ps1 -IP 172.16.0.43
# Or:    .\tools\fleet_deploy.ps1 -All   (deploys to all registry nodes)

param(
    [string]$IP,
    [string]$Env = "heltec_v4",
    [switch]$BuildOnly,
    [switch]$All
)

$PIO = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
# Robustly find espota.py
$ESPOTA = Get-ChildItem -Path "$env:USERPROFILE\.platformio\packages" -Filter "espota.py" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if (!$ESPOTA) { $ESPOTA = "python -m espota" } # Fallback

$ROOT = Resolve-Path "$PSScriptRoot\.."
$FW_DIR = "$ROOT\firmware\v2"
$BIN = "$FW_DIR\.pio\build\$Env\firmware.bin"

# Registry: MAC -> IP
$REGISTRY = @{
    "E6C8" = "172.16.0.27"
    "A4B8" = "172.16.0.29"
    "97D4" = "172.16.0.43"
    "7E34" = "172.16.0.30"
}

function Build {
    Write-Host ""
    Write-Host "=== BUILD: $Env ===" -ForegroundColor Cyan
    & $PIO run -e $Env -d $FW_DIR
    if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED" -ForegroundColor Red; exit 1 }
    $size = (Get-Item $BIN).Length / 1KB
    Write-Host "Binary: $BIN ($([int]$size) KB)" -ForegroundColor Green
}

function Deploy($targetIP) {
    Write-Host ""
    Write-Host "=== UPLOAD -> $targetIP ===" -ForegroundColor Cyan
    python $ESPOTA -i $targetIP -p 3232 -f $BIN
    if ($LASTEXITCODE -eq 0) {
        Write-Host "SUCCESS: $targetIP" -ForegroundColor Green
        Start-Sleep -Seconds 8  # wait for reboot
        $ver = (curl --max-time 3 "http://$targetIP/api/version" 2>$null | ConvertFrom-Json).version
        if ($ver) { Write-Host "Verified: $targetIP = v$ver" -ForegroundColor Green }
    } else {
        Write-Host "FAILED: $targetIP" -ForegroundColor Red
    }
}

# --- Main ---

# Always build unless binary already exists and no source changes
if (!(Test-Path $BIN) -or $BuildOnly) {
    Build
} else {
    $binAge = (Get-Date) - (Get-Item $BIN).LastWriteTime
    Write-Host "Reusing existing binary (age: $([int]$binAge.TotalMinutes) min): $BIN" -ForegroundColor Yellow
}

if ($BuildOnly) { exit 0 }

if ($All) {
    foreach ($entry in $REGISTRY.GetEnumerator()) {
        Deploy $entry.Value
    }
} elseif ($IP) {
    Deploy $IP
} else {
    Write-Host "Usage: .\tools\fleet_deploy.ps1 -IP <ip> [-Env heltec_v4] [-BuildOnly] [-All]" -ForegroundColor Yellow
    Write-Host "Registry:" -ForegroundColor Yellow
    $REGISTRY.GetEnumerator() | ForEach-Object { Write-Host "  $($_.Key) -> $($_.Value)" }
}
