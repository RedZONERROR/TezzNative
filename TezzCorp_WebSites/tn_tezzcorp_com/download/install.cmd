@echo off
setlocal
set "TEZZ_INSTALL_URL=https://tezznative.org/install.ps1?nocache=%RANDOM%%RANDOM%"
set "TEZZ_INSTALL_TMP=%TEMP%\tezznative-install-%RANDOM%%RANDOM%.ps1"

where powershell >nul 2>nul
if errorlevel 1 (
  echo PowerShell is required for TezzNative installation.
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "try { Invoke-WebRequest -UseBasicParsing -Uri '%TEZZ_INSTALL_URL%' -OutFile '%TEZZ_INSTALL_TMP%' } catch { exit 1 }"
if errorlevel 1 (
  echo Failed to download TezzNative installer.
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%TEZZ_INSTALL_TMP%" %*
set "TEZZ_EXIT=%ERRORLEVEL%"
del /f /q "%TEZZ_INSTALL_TMP%" >nul 2>nul
endlocal & exit /b %TEZZ_EXIT%
