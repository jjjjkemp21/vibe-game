@echo off
rem Double-click or run from a terminal. Status: Saved\AgentLogs\status\run-tests.json
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-tests.ps1" %*
set RC=%ERRORLEVEL%
echo.
echo run-tests finished with exit code %RC%. Status file: %~dp0..\Saved\AgentLogs\status\run-tests.json
timeout /t 30
exit /b %RC%
