@echo off
REM Ollama Async Task Queue — Fire and forget, check results later
REM Tasks are queued to a log file, processed in background
REM
REM Usage:
REM   ollama_queue.bat "queue" "model" "task_prompt"
REM   ollama_queue.bat "check"
REM   ollama_queue.bat "process"

setlocal enabledelayedexpansion

set "QUEUE_FILE=%APPDATA%\Magic\ollama_queue.txt"
set "RESULTS_DIR=%APPDATA%\Magic\ollama_results"

if not exist "%APPDATA%\Magic" mkdir "%APPDATA%\Magic"
if not exist "%RESULTS_DIR%" mkdir "%RESULTS_DIR%"

if "%~1"=="queue" (
    REM Queue a task
    set "MODEL=%~2"
    set "TASK=%~3"
    set "TASK_ID=task_%random%_%date:~-2%%time:~0,2%%time:~3,2%%time:~6,2%"

    echo [%date% %time%] Task queued: %TASK_ID%
    echo MODEL=%MODEL% >> "%QUEUE_FILE%"
    echo PROMPT=%TASK% >> "%QUEUE_FILE%"
    echo TASK_ID=%TASK_ID% >> "%QUEUE_FILE%"
    echo --- >> "%QUEUE_FILE%"

    echo Task ID: %TASK_ID%
    exit /b 0
)

if "%~1"=="check" (
    REM Check results
    echo Checking queued results...
    dir /b "%RESULTS_DIR%" 2>nul
    exit /b 0
)

if "%~1"=="process" (
    REM Process all queued tasks (run in background)
    echo Processing queued tasks...

    REM PowerShell script to process queue
    powershell -Command ^
        "$queueFile = '%QUEUE_FILE%'; ^
        $resultsDir = '%RESULTS_DIR%'; ^
        if (Test-Path $queueFile) { ^
            $lines = @(Get-Content $queueFile); ^
            $taskId = ''; $model = ''; $prompt = ''; ^
            foreach ($line in $lines) { ^
                if ($line -match '^TASK_ID=(.+)$') { $taskId = $Matches[1] }; ^
                if ($line -match '^MODEL=(.+)$') { $model = $Matches[1] }; ^
                if ($line -match '^PROMPT=(.+)$') { $prompt = $Matches[1] }; ^
                if ($line -eq '---' -and $taskId) { ^
                    Write-Host \"Processing $taskId with $model...\"; ^
                    $request = @{'model' = $model; 'prompt' = $prompt; 'stream' = $false}; ^
                    $response = Invoke-RestMethod -Uri 'http://localhost:11434/api/generate' -Method Post -Body ($request | ConvertTo-Json) -ContentType 'application/json' -TimeoutSec 300; ^
                    $resultFile = Join-Path $resultsDir \"$taskId.txt\"; ^
                    Add-Content -Path $resultFile -Value $response.response; ^
                    Write-Host \"Result saved to $resultFile\"; ^
                    $taskId = ''; $model = ''; $prompt = '' ^
                } ^
            } ^
            Clear-Content $queueFile ^
        }"

    exit /b 0
)

echo Ollama Async Queue Manager
echo Usage:
echo   ollama_queue.bat queue "model" "task_prompt"  - Queue a task
echo   ollama_queue.bat check                          - Check results
echo   ollama_queue.bat process                        - Process all queued tasks
exit /b 1
