@echo off
REM LoRaLink Ecosystem Entry Point
REM This script will start Docker Desktop and all LoRaLink services.

cd /d "%~dp0"
call tools\Start_Everything.bat
