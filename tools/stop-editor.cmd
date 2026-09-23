@echo off
rem Double-click or run from a terminal. Status: Saved\AgentLogs\status\stop-editor.json
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0stop-editor.ps1" %*
set RC=%ERRORLEVEL%
echo.
echo stop-editor finished with exit code %RC%. Status file: %~dp0..\Saved\AgentLogs\status\stop-editor.json
timeout /t 30
exit /b %RC%
