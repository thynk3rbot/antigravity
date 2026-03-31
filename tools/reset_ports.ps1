# Magic Port Reset Utility
Write-Host "--- Releasing COM Port Locks ---" -ForegroundColor Cyan

# Terminate WebApp backend
Write-Host "Terminating server.py..." -ForegroundColor Yellow
Get-WmiObject Win32_Process | Where-Object { $_.CommandLine -match "server\.py" } | ForEach-Object { 
    Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue 
    Write-Host "Killed process $($_.ProcessId)" -ForegroundColor DarkGray
}

# Terminate any hung PIO processes
Write-Host "Terminating pio.exe..." -ForegroundColor Yellow
Get-Process pio -ErrorAction SilentlyContinue | Stop-Process -Force

# Terminate any hung esptool processes
Write-Host "Terminating esptool.exe..." -ForegroundColor Yellow
Get-Process esptool -ErrorAction SilentlyContinue | Stop-Process -Force

Write-Host "--- Ports Released. Ready for Flash ---" -ForegroundColor Green
