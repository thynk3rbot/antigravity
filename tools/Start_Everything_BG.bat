@echo off
echo Starting LoRaLink Services in the Background...
start /B pythonw "%~dp0start_bg_services.py"
echo Services are now running silently in the background!
echo (Logs can be found in the webapp, website, and docs folders)
timeout /t 3
