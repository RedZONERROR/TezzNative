@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

set "TEZZC_ENV=%TEZZC%"
set "TEZZC="
set "PROBE=%ROOT%\tools\probes\tls_connect_ex_probe.tn"

if defined TEZZC_ENV if exist "%TEZZC_ENV%" (
  if exist "%PROBE%" (
    "%TEZZC_ENV%" check "%PROBE%" >nul 2>nul
    if not errorlevel 1 set "TEZZC=%TEZZC_ENV%"
  ) else (
    set "TEZZC=%TEZZC_ENV%"
  )
)
if not defined TEZZC if exist "%ROOT%\bin\tezzc.exe" (
  if exist "%PROBE%" (
    "%ROOT%\bin\tezzc.exe" check "%PROBE%" >nul 2>nul
    if not errorlevel 1 set "TEZZC=%ROOT%\bin\tezzc.exe"
  ) else (
    set "TEZZC=%ROOT%\bin\tezzc.exe"
  )
)
if not defined TEZZC if exist "%ROOT%\bin\tezzc-windows-x64.exe" (
  if exist "%PROBE%" (
    "%ROOT%\bin\tezzc-windows-x64.exe" check "%PROBE%" >nul 2>nul
    if not errorlevel 1 set "TEZZC=%ROOT%\bin\tezzc-windows-x64.exe"
  ) else (
    set "TEZZC=%ROOT%\bin\tezzc-windows-x64.exe"
  )
)
if not defined TEZZC if exist "%ROOT%\TezzNative-language\bin\tezzc.exe" (
  if exist "%PROBE%" (
    "%ROOT%\TezzNative-language\bin\tezzc.exe" check "%PROBE%" >nul 2>nul
    if not errorlevel 1 set "TEZZC=%ROOT%\TezzNative-language\bin\tezzc.exe"
  ) else (
    set "TEZZC=%ROOT%\TezzNative-language\bin\tezzc.exe"
  )
)
if not defined TEZZC if exist "%ROOT%\build\tezzc.exe" (
  if exist "%PROBE%" (
    "%ROOT%\build\tezzc.exe" check "%PROBE%" >nul 2>nul
    if not errorlevel 1 set "TEZZC=%ROOT%\build\tezzc.exe"
  ) else (
    set "TEZZC=%ROOT%\build\tezzc.exe"
  )
)
if not defined TEZZC if exist "%ROOT%\tezzc.exe" (
  if exist "%PROBE%" (
    "%ROOT%\tezzc.exe" check "%PROBE%" >nul 2>nul
    if not errorlevel 1 set "TEZZC=%ROOT%\tezzc.exe"
  ) else (
    set "TEZZC=%ROOT%\tezzc.exe"
  )
)
if not defined TEZZC for /f "delims=" %%P in ('where tezzc.exe 2^>nul') do if not defined TEZZC (
  if exist "%PROBE%" (
    "%%P" check "%PROBE%" >nul 2>nul
    if not errorlevel 1 set "TEZZC=%%P"
  ) else (
    set "TEZZC=%%P"
  )
)
if not defined TEZZC for /f "delims=" %%P in ('where tezzc-windows-x64.exe 2^>nul') do if not defined TEZZC (
  if exist "%PROBE%" (
    "%%P" check "%PROBE%" >nul 2>nul
    if not errorlevel 1 set "TEZZC=%%P"
  ) else (
    set "TEZZC=%%P"
  )
)
if not defined TEZZC if exist "%ROOT%\tools\build_core_strict.ps1" (
  powershell -ExecutionPolicy Bypass -File "%ROOT%\tools\build_core_strict.ps1" >nul 2>nul
  if not defined TEZZC if exist "%ROOT%\bin\tezzc.exe" (
    if exist "%PROBE%" (
      "%ROOT%\bin\tezzc.exe" check "%PROBE%" >nul 2>nul
      if not errorlevel 1 set "TEZZC=%ROOT%\bin\tezzc.exe"
    ) else (
      set "TEZZC=%ROOT%\bin\tezzc.exe"
    )
  )
  if not defined TEZZC if exist "%ROOT%\build\tezzc.exe" (
    if exist "%PROBE%" (
      "%ROOT%\build\tezzc.exe" check "%PROBE%" >nul 2>nul
      if not errorlevel 1 set "TEZZC=%ROOT%\build\tezzc.exe"
    ) else (
      set "TEZZC=%ROOT%\build\tezzc.exe"
    )
  )
)

if not defined TEZZC if defined TEZZC_ENV if exist "%TEZZC_ENV%" set "TEZZC=%TEZZC_ENV%"
if not defined TEZZC if exist "%ROOT%\bin\tezzc.exe" set "TEZZC=%ROOT%\bin\tezzc.exe"
if not defined TEZZC if exist "%ROOT%\bin\tezzc-windows-x64.exe" set "TEZZC=%ROOT%\bin\tezzc-windows-x64.exe"
if not defined TEZZC if exist "%ROOT%\TezzNative-language\bin\tezzc.exe" set "TEZZC=%ROOT%\TezzNative-language\bin\tezzc.exe"
if not defined TEZZC if exist "%ROOT%\build\tezzc.exe" set "TEZZC=%ROOT%\build\tezzc.exe"
if not defined TEZZC if exist "%ROOT%\tezzc.exe" set "TEZZC=%ROOT%\tezzc.exe"
if not defined TEZZC for /f "delims=" %%P in ('where tezzc.exe 2^>nul') do if not defined TEZZC set "TEZZC=%%P"
if not defined TEZZC for /f "delims=" %%P in ('where tezzc-windows-x64.exe 2^>nul') do if not defined TEZZC set "TEZZC=%%P"

if not defined TEZZC (
  echo tezz: compiler not found ^(expected bin\tezzc.exe, a source build via tools\build_core_strict.ps1, or PATH tezzc.exe^).
  exit /b 1
)

if /I "%~1"=="serve" goto :do_serve
if /I "%~1"=="status" (
  "%TEZZC%" %*
  exit /b %ERRORLEVEL%
)

set "TOOL=%ROOT%\tools\tezz.tn"
if not exist "%TOOL%" (
  echo tezz: tools\tezz.tn not found next to launcher.
  exit /b 1
)

set "PATH=%ROOT%\bin;%ROOT%\build;%ROOT%;%PATH%"
set "TEZZ_SDK_ROOT=%ROOT%"

"%TEZZC%" run --bc "%TOOL%" -- %* --tezzc "%TEZZC%" --sdk-root "%ROOT%"
exit /b %ERRORLEVEL%

:do_serve
set "PATH=%ROOT%\bin;%ROOT%\build;%ROOT%;%PATH%"
set "TEZZ_SDK_ROOT=%ROOT%"

if /I "%~2"=="status" (
  "%TEZZC%" status
  exit /b %ERRORLEVEL%
)

set "TEZZ_SERVER_PORT=8000"
set "TEZZ_SERVER_DB="
set "TEZZ_SERVER_DB_PATH="
set "_A2=%~2"
set "_A3=%~3"
set "_A4=%~4"

if defined _A2 (
  if /I "%_A2%"=="--db" (
    set "TEZZ_SERVER_DB=1"
  ) else (
    set "TEZZ_SERVER_PORT=%_A2%"
  )
)
if defined _A2 if /I "%_A2%"=="--db" (
  if defined _A3 set "TEZZ_SERVER_DB_PATH=%_A3%"
)
if defined _A2 if not /I "%_A2%"=="--db" (
  if defined _A3 if /I "%_A3%"=="--db" (
    set "TEZZ_SERVER_DB=1"
    if defined _A4 set "TEZZ_SERVER_DB_PATH=%_A4%"
  )
)
if defined TEZZ_SERVER_DB if not defined TEZZ_SERVER_DB_PATH (
  set "TEZZ_SERVER_DB_PATH=tezz_app.tdb"
)

"%TEZZC%" run --bc "%ROOT%\lib\tezz_http_server.tn"
exit /b %ERRORLEVEL%
