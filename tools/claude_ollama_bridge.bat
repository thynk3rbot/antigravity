@echo off
REM Claude ↔ Ollama Bridge — Claude directs local model on simple tasks
REM This batch file receives tasks from Claude and executes them using qwen2.5-coder
REM
REM Usage: claude_ollama_bridge.bat "task_type" "task_description"
REM Types: search-replace, generate-code, analyze, find-files, refactor
REM
REM Example:
REM   claude_ollama_bridge.bat "search-replace" "Replace all #include <WiFi.h> with #include \"WiFi.h\" in firmware/v2/lib/**/*.cpp"
REM   claude_ollama_bridge.bat "generate-code" "Write a function to validate GPIO pins 0-49 with error handling"

setlocal enabledelayedexpansion

if "%~2"=="" (
    echo Claude-Ollama Bridge v1.0
    echo Usage: claude_ollama_bridge.bat "task_type" "task_description"
    echo.
    echo Task Types:
    echo   search-replace    - Search and replace patterns in files
    echo   generate-code     - Generate code snippets
    echo   analyze           - Analyze code for issues
    echo   find-files        - Find files matching criteria
    echo   refactor          - Suggest refactoring
    echo.
    exit /b 1
)

set "TASK_TYPE=%~1"
set "TASK_DESC=%~2"
set "MODEL=qwen2.5-coder:14b"
set "OLLAMA_URL=http://localhost:11434/api/generate"

echo.
echo ================================================================
echo Claude → Ollama Bridge
echo Task Type: %TASK_TYPE%
echo Model: %MODEL%
echo ================================================================
echo.

REM PowerShell to make the API call and handle response
powershell -Command ^
    "$task = '%TASK_DESC%'; ^
    $request = @{ ^
        'model' = '%MODEL%'; ^
        'prompt' = 'You are a code assistant helping a developer. Task: ' + $task + '\n\nRespond with ONLY the result/code, no explanation or preamble.'; ^
        'stream' = $false ^
    }; ^
    $json = $request | ConvertTo-Json; ^
    $response = Invoke-RestMethod -Uri '%OLLAMA_URL%' -Method Post -Body $json -ContentType 'application/json'; ^
    Write-Host $response.response"

echo.
echo ================================================================
echo [%date% %time%] Task complete
echo ================================================================
echo.

exit /b 0
