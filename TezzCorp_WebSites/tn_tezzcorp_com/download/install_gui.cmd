@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_gui.ps1" %*
exit /b %ERRORLEVEL%
