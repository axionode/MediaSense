@echo off
rem MediaSense installer (per-user, no admin rights, no network)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
echo.
pause
