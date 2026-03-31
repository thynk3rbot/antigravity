@echo off
REM Factory USB Flasher — Virgin Device Commissioning
REM Usage: Double-click to interactively flash a single device
REM        Or run: Factory_USB_Flasher.bat --batch devices.csv

setlocal enabledelayedexpansion

cd /d "%~dp0"

python tools/usb_flasher.py %*

pause
