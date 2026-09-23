@echo off
rem Double-click or run from a terminal. Status: Saved\AgentLogs\status\configure-unreal.json
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0configure-unreal.ps1" %*
set RC=%ERRORLEVEL%
echo.
echo configure-unreal finished with exit code %RC%. Status file: %~dp0..\Saved\AgentLogs\status\configure-unreal.json
timeout /t 30
exit /b %RC%
