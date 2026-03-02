@echo off
REM Two-Device Configuration Deployment
REM Deploys Master (generic) and Slave (farm) configs

cd /d "%~dp0"
echo.
echo ========================================
echo  Deploying configs to both devices...
echo ========================================
echo.

python tools/deploy_configs.py

if %errorlevel% equ 0 (
    echo.
    echo ========================================
    echo  Deployment complete!
    echo ========================================
    pause
) else (
    echo.
    echo ========================================
    echo  Deployment failed
    echo ========================================
    pause
    exit /b 1
)
