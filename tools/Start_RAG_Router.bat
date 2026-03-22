@echo off
TITLE RAG Router Service
echo =======================================================
echo          LoRaLink RAG Router (Port 8200)
echo =======================================================
echo.
cd /d "%~dp0\rag_router"
python server.py
pause
