@echo off
REM Ollama Local Model Worker — Directs local GPU model on repetitive tasks
REM Usage: ollama_worker.bat "task description"
REM Example: ollama_worker.bat "Search and replace all #include <WiFi.h> with #include \"WiFi.h\" in firmware/v2/lib/**/*.cpp"

setlocal enabledelayedexpansion

if "%~1"=="" (
    echo Usage: ollama_worker.bat "task description"
    echo.
    echo Examples:
    echo   ollama_worker.bat "Find all files containing 'TODO' in firmware/v2/lib/"
    echo   ollama_worker.bat "Generate a function that validates GPIO pin numbers 0-49"
    echo   ollama_worker.bat "Search and replace GPIO 21 with GPIO_VEXT in all .h files"
    exit /b 1
)

REM Task description passed as argument
set "TASK=%~1"

REM Call Ollama API (localhost:11434)
REM Model: ollama list (to see what's loaded)

echo [%date% %time%] Task: %TASK%
echo.

REM Create temporary request file
set "TEMP_REQ=%temp%\ollama_request_%random%.json"
set "TEMP_OUT=%temp%\ollama_response_%random%.json"

REM Build JSON request (escape quotes)
(
    echo {
    echo   "model": "neural-chat",
    echo   "prompt": "You are a code assistant. Complete this task: %TASK%\n\nRespond with only the code/result, no explanation.",
    echo   "stream": false
    echo }
) > "%TEMP_REQ%"

REM Call Ollama
echo Calling Ollama...
curl -s -X POST http://localhost:11434/api/generate ^
  -H "Content-Type: application/json" ^
  -d @"%TEMP_REQ%" > "%TEMP_OUT%"

REM Parse response (basic extraction of "response" field)
echo.
echo === OLLAMA RESPONSE ===
echo.

REM Use PowerShell to extract JSON (more reliable)
powershell -Command "$json = Get-Content '%TEMP_OUT%' | ConvertFrom-Json; Write-Host $json.response"

REM Cleanup
del /q "%TEMP_REQ%" 2>nul
del /q "%TEMP_OUT%" 2>nul

echo.
echo [%date% %time%] Task complete

exit /b 0
