@echo off
rem Double-click or run from a terminal. Status: Saved\AgentLogs\status\build.json
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
set RC=%ERRORLEVEL%
echo.
echo build finished with exit code %RC%. Status file: %~dp0..\Saved\AgentLogs\status\build.json
timeout /t 30
exit /b %RC%
