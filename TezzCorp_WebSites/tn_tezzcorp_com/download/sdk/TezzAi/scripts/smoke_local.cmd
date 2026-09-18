@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0..\.."
pushd "%ROOT%" >nul

set "TEZZ=%ROOT%\tezz.cmd"
if defined TEZZ_CMD set "TEZZ=%TEZZ_CMD%"

set "MODEL=TezzAi\data\model.taim"
if exist "build\ai_local\model.taim" set "MODEL=build\ai_local\model.taim"
if defined TEZZ_AI_MODEL set "MODEL=%TEZZ_AI_MODEL%"

set "CODEBOOK=TezzAi\data\codebook.tnxb"
if exist "build\ai_local\codebook.tnxb" set "CODEBOOK=build\ai_local\codebook.tnxb"
if defined TEZZ_AI_CODEBOOK set "CODEBOOK=%TEZZ_AI_CODEBOOK%"

set "HOST=127.0.0.1"
if defined TEZZ_AI_HOST set "HOST=%TEZZ_AI_HOST%"
set "PORT=8099"
if defined TEZZ_AI_PORT set "PORT=%TEZZ_AI_PORT%"

set "PROMPT=build%%20api%%20websocket%%20route%%20with%%20auth%%20middleware"
if defined TEZZ_AI_SMOKE_PROMPT set "PROMPT=%TEZZ_AI_SMOKE_PROMPT%"

set "LOG=build\ai_local\serve_smoke_%RANDOM%_%RANDOM%.log"
if not exist "build\ai_local" mkdir "build\ai_local" >nul 2>nul
del /q "%LOG%" >nul 2>nul

set "HAS_CURL=0"
where curl >nul 2>nul
if not errorlevel 1 set "HAS_CURL=1"

echo [TezzAi smoke] start serve
echo   host:     %HOST%
echo   port:     %PORT%
echo   model:    %MODEL%
echo   codebook: %CODEBOOK%

start "TezzAiServeSmoke" /B cmd /c ""%TEZZ%" ai serve --host "%HOST%" --port "%PORT%" --model "%MODEL%" --codebook "%CODEBOOK%" --max-clients 3 --v2 > "%LOG%" 2>&1"

set "HEALTH="
for /L %%I in (1,1,30) do (
  call :http_get "http://%HOST%:%PORT%/health" HEALTH
  if defined HEALTH goto :health_ok
  <nul set /p "=."
  timeout /t 1 /nobreak >nul
)
echo(

echo [TezzAi smoke] FAIL: /health did not respond
echo --- serve log ---
type "%LOG%" 2>nul
goto :fail

:health_ok
echo !HEALTH! | findstr /C:"\"ok\":true" >nul
if errorlevel 1 (
  echo [TezzAi smoke] FAIL: /health missing ok=true
  echo !HEALTH!
  goto :fail
)
echo !HEALTH! | findstr /C:"\"identity\"" >nul
if errorlevel 1 (
  echo [TezzAi smoke] FAIL: /health missing identity payload
  echo !HEALTH!
  goto :fail
)

set "CODEJSON="
call :http_get "http://%HOST%:%PORT%/ai/code?prompt=%PROMPT%" CODEJSON
if not defined CODEJSON (
  echo [TezzAi smoke] FAIL: /ai/code returned empty response
  goto :fail
)
echo !CODEJSON! | findstr /C:"\"ok\":true" >nul
if errorlevel 1 (
  echo [TezzAi smoke] FAIL: /ai/code missing ok=true
  echo !CODEJSON!
  goto :fail
)
echo !CODEJSON! | findstr /C:"\"code\"" >nul
if errorlevel 1 (
  echo [TezzAi smoke] FAIL: /ai/code missing code payload
  echo !CODEJSON!
  goto :fail
)

set "ROOTHTML="
call :http_get "http://%HOST%:%PORT%/" ROOTHTML
if not defined ROOTHTML (
  echo [TezzAi smoke] FAIL: / returned empty response
  goto :fail
)
echo !ROOTHTML! | findstr /C:"TezzAi Local" >nul
if errorlevel 1 (
  echo [TezzAi smoke] FAIL: / did not return TezzAi UI
  echo !ROOTHTML!
  goto :fail
)

echo [TezzAi smoke] PASS
echo   /health and /ai/code validated
call :stop_server
popd >nul
exit /b 0

:fail
echo --- serve log ---
type "%LOG%" 2>nul
call :stop_server
popd >nul
exit /b 1

:http_get
setlocal EnableDelayedExpansion
set "URL=%~1"
set "OUT="
if "%HAS_CURL%"=="1" (
  for /f "usebackq delims=" %%H in (`curl -sS --connect-timeout 2 --max-time 4 "!URL!" 2^>nul`) do (
    set "OUT=%%H"
  )
) else (
  for /f "usebackq delims=" %%H in (`powershell -NoProfile -Command "(Invoke-WebRequest -UseBasicParsing -Uri '!URL!' -TimeoutSec 4).Content" 2^>nul`) do (
    set "OUT=%%H"
  )
)
endlocal & set "%~2=%OUT%"
exit /b 0

:stop_server
taskkill /FI "WINDOWTITLE eq TezzAiServeSmoke" /T /F >nul 2>nul
exit /b 0
