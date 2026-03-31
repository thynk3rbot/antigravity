@echo off
TITLE Package Release
echo =======================================================
echo          Magic Release Packager
echo =======================================================
echo.
echo Packaging Magic into Release.zip...

REM Check if ngrok token is set, warn if it's not
if not exist ".env" (
    echo.
    echo [WARNING] No .env file found. To use the public ngrok tunnel, create a .env file with NGROK_AUTHTOKEN=your_token
    echo.
)

cd /d "%~dp0\.."

REM Use PowerShell to zip the necessary folders/files
echo Compressing folders...
powershell -Command "Compress-Archive -Path Start_Magic.bat, docker-compose.production.yml, mqttdocker.yml, tools, docs -DestinationPath Release.zip -Force"

if exist Release.zip (
    echo.
    echo [SUCCESS] Release.zip created! You can now share this file.
) else (
    echo.
    echo [ERROR] Failed to create Release.zip.
)

echo.
pause
