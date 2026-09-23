@echo off
rem Double-click or run from a terminal. Status: Saved\AgentLogs\status\launch-editor.json
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch-editor.ps1" %*
set RC=%ERRORLEVEL%
echo.
echo launch-editor finished with exit code %RC%. Status file: %~dp0..\Saved\AgentLogs\status\launch-editor.json
timeout /t 30
exit /b %RC%
