@echo off
REM Antigravity Firmware Version Manager for Windows
REM Usage: version <command> [args]
REM Example: version check 0.0.00-2

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."
set "VERSION_FILE=%REPO_ROOT%\.version"

if "%1"=="" goto :help
if "%1"=="help" goto :help
if "%1"=="-h" goto :help
if "%1"=="--help" goto :help

if "%1"=="check" goto :check
if "%1"=="current" goto :current
if "%1"=="set" goto :set
if "%1"=="bump" goto :bump
if "%1"=="validate-all" goto :validate_all

echo [ERROR] Unknown command: %1
goto :help

:check
if "%2"=="" (
    echo [ERROR] Usage: version check ^<version^>
    echo Example: version check 0.0.00-2
    exit /b 1
)
setlocal enabledelayedexpansion
set "version=%2"
if not "!version:~0,1!"=="" (
    REM Simple regex check for format X.X.XX-2,3,4
    echo !version! | findstr /R "^[0-9]\+\.[0-9]\+\.[0-9][0-9]-[234]$" >nul
    if errorlevel 1 (
        echo [ERROR] Invalid version format: !version!
        echo Expected format: MAJOR.MINOR.POINT-PLATFORM
        echo Example: 0.0.00-2
        exit /b 1
    )
    echo [OK] Version format valid: !version!
    exit /b 0
)
exit /b 1

:current
if "%2"=="" (
    echo [ERROR] Usage: version current ^<platform^>
    echo Platforms: 2, 3, 4
    exit /b 1
)
if not exist "%VERSION_FILE%" (
    echo [WARNING] No version file found
    exit /b 1
)
for /f "tokens=*" %%a in ('findstr ".*-!platform!$" "%VERSION_FILE%" 2^>nul ^| findstr /v "^$"') do (
    set "found_version=%%a"
)
if defined found_version (
    echo !found_version!
    exit /b 0
) else (
    echo [WARNING] No version found for platform %2
    exit /b 1
)

:set
if "%2"=="" (
    echo [ERROR] Usage: version set ^<version^>
    echo Example: version set 0.0.00-2
    exit /b 1
)
setlocal enabledelayedexpansion
set "version=%2"
echo !version! | findstr /R "^[0-9]\+\.[0-9]\+\.[0-9][0-9]-[234]$" >nul
if errorlevel 1 (
    echo [ERROR] Invalid version format: !version!
    exit /b 1
)
if not exist "%VERSION_FILE%" (
    type nul > "%VERSION_FILE%"
)
>> "%VERSION_FILE%" echo !version!
echo [OK] Version set to: !version!
exit /b 0

:bump
if "%2"=="" (
    echo [ERROR] Usage: version bump ^<type^> ^<platform^>
    echo Type: major, minor, point
    echo Platform: 2, 3, 4
    exit /b 1
)
echo Bump functionality requires bash. Please use WSL or Git Bash.
exit /b 1

:validate_all
if not exist "%VERSION_FILE%" (
    echo [WARNING] No version file found
    exit /b 1
)
echo [INFO] Validating all versions...
for /f "tokens=*" %%a in (%VERSION_FILE%) do (
    set "version=%%a"
    echo !version! | findstr /R "^[0-9]\+\.[0-9]\+\.[0-9][0-9]-[234]$" >nul
    if errorlevel 1 (
        echo [ERROR] Invalid format: !version!
        exit /b 1
    ) else (
        echo [OK] !version!
    )
)
echo [SUCCESS] All versions are valid!
exit /b 0

:help
echo Antigravity Firmware Version Manager
echo.
echo USAGE:
echo     version ^<command^> [options]
echo.
echo COMMANDS:
echo     check ^<version^>         Validate version format
echo                            Example: version check 0.0.05-2
echo.
echo     current ^<platform^>      Show current version for platform
echo                            Example: version current 2
echo.
echo     set ^<version^>          Set/update version
echo                            Example: version set 0.1.00-3
echo.
echo     validate-all           Validate all versions in .version file
echo.
echo     help                   Show this help message
echo.
echo VERSION FORMAT:
echo     MAJOR.MINOR.POINT-PLATFORM
echo.
echo     MAJOR      = Major version (0, 1, 2, ...)
echo     MINOR      = Minor version (0, 1, 2, ...)
echo     POINT      = Point release (00, 01, 02, ..., 99) - zero-padded
echo     PLATFORM   = Target platform (2, 3, or 4)
echo.
echo EXAMPLES:
echo     version check 0.0.00-2       # Validate version format
echo     version set 0.0.00-2         # Initialize version for platform 2
echo     version current 2            # Show current version for platform 2
echo     version validate-all         # Check all versions are valid
echo.
echo PLATFORMS:
echo     2 = Platform 2
echo     3 = Platform 3
echo     4 = Platform 4
echo.
exit /b 0
