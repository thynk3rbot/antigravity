@echo off
REM Search & Replace Batch Tool — Uses Ollama for pattern generation
REM Usage: search_replace_batch.bat "pattern" "replacement" "file_pattern"
REM Example: search_replace_batch.bat "RELAY_CH6.*21" "RELAY_CH6   255" "*.h"

setlocal enabledelayedexpansion

if "%~3"=="" (
    echo Usage: search_replace_batch.bat "search_pattern" "replacement" "file_glob"
    echo.
    echo Examples:
    echo   search_replace_batch.bat "RELAY_CH6   21" "RELAY_CH6   255" "firmware/v2/**/*.h"
    echo   search_replace_batch.bat "#include ^<WiFi.h^>" "#include \"WiFi.h\"" "**/*.cpp"
    echo   search_replace_batch.bat "digitalWrite(VEXT_PIN, HIGH)" "digitalWrite(VEXT_PIN, LOW)" "**/*.cpp"
    exit /b 1
)

set "SEARCH=%~1"
set "REPLACE=%~2"
set "FILE_GLOB=%~3"

echo [%date% %time%] Starting batch search/replace
echo Search: %SEARCH%
echo Replace: %REPLACE%
echo Files: %FILE_GLOB%
echo.

REM PowerShell script to do the actual replacement
powershell -Command ^
    "Get-ChildItem -Path '%cd%' -Filter '%FILE_GLOB%' -Recurse | ForEach-Object { ^
        $content = Get-Content $_.FullName -Raw; ^
        if ($content -match '%SEARCH%') { ^
            Write-Host 'Processing: ' $_.FullName; ^
            $newContent = $content -replace '%SEARCH%', '%REPLACE%'; ^
            Set-Content -Path $_.FullName -Value $newContent; ^
            Write-Host '  ✓ Updated'; ^
        } ^
    }"

echo.
echo [%date% %time%] Batch complete
exit /b 0
