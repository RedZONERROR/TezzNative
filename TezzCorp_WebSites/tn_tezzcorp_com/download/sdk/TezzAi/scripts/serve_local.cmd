@echo off
setlocal EnableExtensions

set "ROOT=%~dp0..\.."
pushd "%ROOT%" >nul

set "TEZZ=%ROOT%\tezz.cmd"
if defined TEZZ_CMD set "TEZZ=%TEZZ_CMD%"

set "MODEL=TezzAi\data\model.taim"
if exist "build\ai_local\model.taim" set "MODEL=build\ai_local\model.taim"
if defined TEZZ_AI_MODEL set "MODEL=%TEZZ_AI_MODEL%"
set "DB=TezzAi\data\llm_tezzdb.tdb"
if exist "build\ai_local\llm_tezzdb.tdb" set "DB=build\ai_local\llm_tezzdb.tdb"
if defined TEZZ_AI_DB set "DB=%TEZZ_AI_DB%"
set "CODEBOOK=TezzAi\data\codebook.tnxb"
if exist "build\ai_local\codebook.tnxb" set "CODEBOOK=build\ai_local\codebook.tnxb"
if defined TEZZ_AI_CODEBOOK set "CODEBOOK=%TEZZ_AI_CODEBOOK%"
set "MODE=llm"
if defined TEZZ_AI_MODE set "MODE=%TEZZ_AI_MODE%"
set "HOST=127.0.0.1"
if defined TEZZ_AI_HOST set "HOST=%TEZZ_AI_HOST%"
set "PORT=8099"
if defined TEZZ_AI_PORT set "PORT=%TEZZ_AI_PORT%"
set "OPEN_BROWSER=1"
if defined TEZZ_AI_NO_OPEN set "OPEN_BROWSER=0"
set "OPEN_WAIT_SECS=20"
if defined TEZZ_AI_OPEN_WAIT set "OPEN_WAIT_SECS=%TEZZ_AI_OPEN_WAIT%"

echo [TezzAi] local server start
echo   host:     %HOST%
echo   port:     %PORT%
echo   mode:     %MODE%
echo   model:    %MODEL%
if /I "%MODE%"=="coding" (
  echo   codebook: %CODEBOOK%
) else (
  echo   tezzdb:   %DB%
)
echo   ui:       http://%HOST%:%PORT%/
echo(
if "%OPEN_BROWSER%"=="1" (
  start "" powershell -NoProfile -WindowStyle Hidden -Command "$health='http://%HOST%:%PORT%/health'; $ui='http://%HOST%:%PORT%/'; $max=%OPEN_WAIT_SECS%; for($i=0;$i -lt $max;$i++){ try { $r=Invoke-WebRequest -UseBasicParsing -Uri $health -TimeoutSec 1; if($r.StatusCode -ge 200 -and $r.StatusCode -lt 500){ Start-Process $ui; exit 0 } } catch {} Start-Sleep -Seconds 1 }; Start-Process $ui"
  echo Browser auto-open scheduled (waits for /health readiness).
  echo If it does not open, run: start "" "http://%HOST%:%PORT%/"
)
echo Press Ctrl+C to stop.
echo(

if /I "%MODE%"=="coding" (
  call "%TEZZ%" ai serve --host "%HOST%" --port "%PORT%" --model "%MODEL%" --codebook "%CODEBOOK%" --v2
) else (
  call "%TEZZ%" ai llm serve --host "%HOST%" --port "%PORT%" --model "%MODEL%" --db "%DB%"
)
set "RC=%ERRORLEVEL%"
popd >nul
exit /b %RC%
