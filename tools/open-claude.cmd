@echo off
rem Opens Claude Code in the project root. Extra arguments are passed through, e.g.:  open-claude.cmd --continue
cd /d "%~dp0.."
where claude >nul 2>nul
if errorlevel 1 (
  echo Claude Code was not found on PATH. Close this window, open a NEW terminal window, and try again.
  echo If it still fails, rerun C:\GameDev\_setup\run-install-user.cmd
  pause
  exit /b 1
)
claude %*
