@echo off
TITLE Magic FreeCAD MCP Server
color 0E

echo =======================================================
echo          Magic FreeCAD MCP Server
echo =======================================================
echo.

:: Get the directory of this script
set "TOOLS_DIR=%~dp0"
set "MCP_DIR=%TOOLS_DIR%freecad-mcp-main"

if not exist "%MCP_DIR%" (
    echo [ERROR] FreeCAD MCP directory not found at: %MCP_DIR%
    pause
    exit /b 1
)

echo Starting FreeCAD MCP Server...
echo Directory: %MCP_DIR%
echo.

cd /d "%MCP_DIR%"

:: Check if uv is installed
where uv >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] 'uv' is not installed. Please install it with 'winget install astral-sh.uv'
    pause
    exit /b 1
)

:: Run the server
:: Note: This runs in stdio mode by default.
uv run freecad-mcp %*

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Server exited with code %errorlevel%
    pause
)
