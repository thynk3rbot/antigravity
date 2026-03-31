@echo off
REM Phase 50 Ollama Test — Generate complete implementation guide async
REM This demonstrates:
REM 1. Queueing a lengthy task to local model
REM 2. Model processing in background (user can work on other things)
REM 3. Results ready when needed
REM 4. Workflow for shipping product

setlocal enabledelayedexpansion

set "QUEUE_FILE=%APPDATA%\Magic\ollama_queue.txt"
set "RESULTS_DIR=%APPDATA%\Magic\ollama_results"
set "MODEL=qwen2.5-coder:14b"

if not exist "%APPDATA%\Magic" mkdir "%APPDATA%\Magic"
if not exist "%RESULTS_DIR%" mkdir "%RESULTS_DIR%"

echo ================================================================
echo Phase 50 Ollama Test
echo ================================================================
echo.
echo TASK: Generate Phase 50 implementation guide
echo MODEL: %MODEL%
echo.

REM The actual task (substantial, worth async processing)
set "TASK_PROMPT=You are a firmware architect. Generate a COMPLETE Phase 50 implementation guide for Magic Autonomous Mesh Sovereignty. Include: 1) Device firmware requirements (MQTT contract, command types, error handling). 2) Daemon API specification (REST endpoints for mesh control, provisioning, routing). 3) Code skeleton for device mesh command handler. 4) Code skeleton for daemon mesh router. 5) Integration checklist. Keep response detailed and actionable (aim for 2000+ tokens)."

echo Queueing task...
echo TASK_ID=phase50_guide_%random%_%date:~-2%%time:~0,2%%time:~3,2% >> "%QUEUE_FILE%"
echo MODEL=%MODEL% >> "%QUEUE_FILE%"
echo PROMPT=%TASK_PROMPT% >> "%QUEUE_FILE%"
echo --- >> "%QUEUE_FILE%"

for /F "tokens=2 delims==" %%A in ('findstr "TASK_ID=" "%QUEUE_FILE%" ^| findstr /V "^REM" ^| tail -n 1') do (
    set "TASK_ID=%%A"
)

echo Task queued successfully.
echo Task ID: %TASK_ID%
echo.
echo ================================================================
echo WORKFLOW (What happens next):
echo ================================================================
echo.
echo 1. QUEUE (Right now)
echo    - Task added to queue
echo    - Model starts processing in background
echo    - You immediately continue working
echo.
echo 2. BACKGROUND (While you work on other things)
echo    - Ollama processes your task on GPU
echo    - No blocking, no waiting
echo    - Takes ~2-5 minutes for lengthy task
echo.
echo 3. RESULTS READY (When you need them)
echo    - Results file: %RESULTS_DIR%\%TASK_ID%.txt
echo    - Contains complete Phase 50 implementation guide
echo    - Claude can use immediately in daemon coding
echo.
echo 4. SHIP PRODUCT
echo    - Claude: Uses Phase 50 guide to code daemon
echo    - AG: Tests hardware in parallel
echo    - Local Model: Generating more helpers async
echo.
echo ================================================================
echo TO CHECK RESULTS LATER:
echo ================================================================
echo.
echo   type "%RESULTS_DIR%\%TASK_ID%.txt"
echo.
echo OR
echo.
echo   ollama_queue.bat check
echo.
echo ================================================================
echo STARTING PROCESSING...
echo ================================================================
echo.

REM PowerShell to process the queue (runs in background via START command)
REM This allows the batch file to return immediately while processing continues
start /B powershell -NoProfile -Command ^
    "$queueFile = '%QUEUE_FILE%'; ^
    $resultsDir = '%RESULTS_DIR%'; ^
    $model = '%MODEL%'; ^
    $taskId = ''; ^
    $prompt = ''; ^
    Write-Host '[OLLAMA] Starting background processing...'; ^
    if (Test-Path $queueFile) { ^
        $lines = @(Get-Content $queueFile); ^
        foreach ($line in $lines) { ^
            if ($line -match '^TASK_ID=(.+)$') { $taskId = $Matches[1] }; ^
            if ($line -match '^PROMPT=(.+)$') { $prompt = $Matches[1] }; ^
            if ($line -eq '---' -and $taskId -and $prompt) { ^
                Write-Host \"[OLLAMA] Processing: $taskId\"; ^
                Write-Host '[OLLAMA] Sending to model...'; ^
                $request = @{'model' = $model; 'prompt' = $prompt; 'stream' = $false}; ^
                try { ^
                    $response = Invoke-RestMethod -Uri 'http://localhost:11434/api/generate' -Method Post -Body ($request | ConvertTo-Json) -ContentType 'application/json' -TimeoutSec 600; ^
                    $resultFile = Join-Path $resultsDir \"${taskId}.txt\"; ^
                    Set-Content -Path $resultFile -Value $response.response; ^
                    Write-Host \"[OLLAMA] Result saved: $resultFile\"; ^
                    Write-Host \"[OLLAMA] Task complete at $(Get-Date)\"; ^
                } catch { ^
                    Write-Host \"[OLLAMA] ERROR: $_.Exception.Message\"; ^
                }; ^
                $taskId = ''; ^
                $prompt = '' ^
            } ^
        } ^
        Clear-Content $queueFile ^
    }"

echo.
echo ================================================================
echo TASK QUEUED AND PROCESSING IN BACKGROUND
echo ================================================================
echo.
echo While the model processes (next 2-5 minutes):
echo.
echo - Claude can design Phase 50 daemon architecture
echo - AG can plan hardware mesh testing
echo - You can review product roadmap
echo - Local model works on GPU in the background
echo.
echo ================================================================
echo STATUS:
echo ================================================================
echo.
echo Queued tasks: 1
echo Processing: Yes (check Task Manager > GPU usage)
echo Results location: %RESULTS_DIR%
echo.
echo When ready, run:
echo   type "%RESULTS_DIR%\%TASK_ID%.txt"
echo.
echo Or check all results:
echo   ollama_queue.bat check
echo.

exit /b 0
