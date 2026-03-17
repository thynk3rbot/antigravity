@echo off
TITLE Package Release
echo =======================================================
echo          LoRaLink Release Packager
echo =======================================================
echo.
echo Packaging LoRaLink into Release.zip...

REM Check if ngrok token is set, warn if it's not
if not exist ".env" (
    echo.
    echo [WARNING] No .env file found. To use the public ngrok tunnel, create a .env file with NGROK_AUTHTOKEN=your_token
    echo.
)

REM Remove old release if it exists
if exist Release.zip del Release.zip

REM Use PowerShell to zip the necessary folders/files
powershell -Command "Compress-Archive -Path mqttdocker.yml, tools, docs, webapp, website -DestinationPath Release.zip -Force"

if exist Release.zip (
    echo.
    echo [SUCCESS] Release.zip created! You can now share this file.
) else (
    echo.
    echo [ERROR] Failed to create Release.zip.
)

echo.
pause
