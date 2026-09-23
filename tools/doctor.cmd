@echo off
rem Double-click or run from a terminal. Status: Saved\AgentLogs\status\doctor.json
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0doctor.ps1" %*
set RC=%ERRORLEVEL%
echo.
echo doctor finished with exit code %RC%. Status file: %~dp0..\Saved\AgentLogs\status\doctor.json
timeout /t 30
exit /b %RC%
