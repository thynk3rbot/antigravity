@echo off
TITLE LoRaLink Firmware Flasher
color 0E
echo =======================================================
echo          LoRaLink Firmware Builder & Flasher
echo =======================================================
echo.
echo This script will build the current firmware structure and upload it to
echo a connected ESP32 / Heltec device via USB over PlatformIO.
echo.
echo Ensure your device is plugged in before continuing.
echo.
pause
cd /d "%~dp0\.."
echo.
echo Running 'pio run -t upload'...
echo.
pio run -t upload
if errorlevel 1 (
    color 0C
    echo.
    echo [ERROR] PlatformIO build or upload failed. Check the logs above.
    pause
    exit /b %errorlevel%
)
echo.
color 0A
echo [SUCCESS] Firmware Flashed Successfully!
pause
