@echo off
TITLE NutriCalc - Development Environment Setup
color 0A
echo =======================================================
echo   NutriCalc - Development Environment Setup
echo =======================================================
echo.

REM Check Python is installed
echo [*] Checking Python installation...
python --version >nul 2>&1
if %errorlevel% equ 0 (
    for /f "tokens=*" %%i in ('python --version') do set PYTHON_VERSION=%%i
    echo [OK] Python found: %PYTHON_VERSION%
) else (
    color 0C
    echo [ERROR] Python is not installed or not in PATH
    echo.
    echo To fix this:
    echo   1. Install Python 3.6+ from https://python.org
    echo   2. During installation, check "Add Python to PATH"
    echo   3. Run this script again
    echo.
    pause
    exit /b 1
)

REM Navigate to NutriCalc directory
cd /d "%~dp0\..\tools\nutribuddy"
echo.
echo [*] NutriCalc directory: %cd%
echo.

REM Check required files
echo [*] Checking required files...
set missing=0

if exist "static\index.html" (
    echo [OK] App found: static/index.html
) else (
    echo [ERROR] Missing: static/index.html
    set missing=1
)

if exist "docs\index.html" (
    echo [OK] Docs found: docs/index.html
) else (
    echo [ERROR] Missing: docs/index.html
    set missing=1
)

if exist "server.py" (
    echo [OK] Server found: server.py
) else (
    echo [ERROR] Missing: server.py
    set missing=1
)

if exist "chemicals.json" (
    echo [OK] Data found: chemicals.json
) else (
    echo [ERROR] Missing: chemicals.json
    set missing=1
)

if %missing% equ 1 (
    color 0C
    echo.
    echo [ERROR] Some files are missing!
    echo Please verify the NutriCalc installation and try again.
    pause
    exit /b 1
)

REM Validate JSON files
echo.
echo [*] Validating JSON configuration files...
python -m json.tool chemicals.json >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] chemicals.json - Valid
) else (
    color 0C
    echo [ERROR] chemicals.json - Invalid JSON
    set missing=1
)

python -m json.tool mqtt_config.json >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] mqtt_config.json - Valid
) else (
    echo [WARNING] mqtt_config.json - Invalid (will use defaults)
)

if %missing% equ 1 (
    color 0C
    echo.
    echo [ERROR] JSON validation failed!
    pause
    exit /b 1
)

REM Success
color 0B
echo.
echo =======================================================
echo   NutriCalc Setup Complete
echo =======================================================
echo.
echo Environment is ready for development!
echo.
echo Next steps:
echo   1. Run: Start_NutriCalc_All.bat
echo      OR: Start_NutriCalc_App.bat (for app only)
echo      OR: Start_NutriCalc_Docs.bat (for docs only)
echo.
echo   2. Open in browser:
echo      - App: http://localhost:8100/static/
echo      - Docs: http://localhost:8101/
echo.
echo   3. Make changes and test locally
echo      - Edit static/index.html for app changes
echo      - Edit docs/index.html for documentation
echo.
echo   4. Commit and push to deploy to production:
echo      - git add .
echo      - git commit -m "Update: Your changes here"
echo      - git push origin main
echo.
echo   5. View deployment on GitHub Pages:
echo      https://thynk3rbot.github.io/antigravity/static/
echo.
echo For help, see:
echo   - README.md
echo   - DEPLOY.md
echo   - GITHUB_ACTIONS_SETUP.md
echo.
pause
