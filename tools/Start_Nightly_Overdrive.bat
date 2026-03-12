@echo off
TITLE LoRaLink Nightly Overdrive

echo =========================================================
echo          LoRaLink Nightly Endurance ^& Analysis
echo =========================================================
echo.
echo This protocol runs automated tests repeatedly to simulate 
echo heavy usage overnight, logging response times and failures.
echo It will automatically generate a TODO markdown file in:
echo tools\testing\logs for your morning review.
echo.

:: You can customize these arguments freely
:: --cycles: number of full regression loops to run
:: --delay: wait time in seconds before next loop
:: --ip: the IP of the local master gateway

set "CYCLES=50"
set "DELAY=300"
set "IP=127.0.0.1:8000"
:: Switch the IP to your Master Gateway IP if doing real hardware loop (172.16.0.27)

cd /d "%~dp0"
python testing\overdrive.py --cycles %CYCLES% --delay %DELAY% --ip %IP%

echo.
echo Processing Complete. Check tools\testing\logs for your action items!
pause
