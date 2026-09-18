@echo off
setlocal EnableExtensions

set "ROOT=%~dp0..\.."
pushd "%ROOT%" >nul

set "TEZZ=%ROOT%\tezz.cmd"
if defined TEZZ_CMD set "TEZZ=%TEZZ_CMD%"

set "DATA=TezzAi\data\samples.tnxb"
if defined TEZZ_AI_DATA set "DATA=%TEZZ_AI_DATA%"
set "MODEL=TezzAi\data\model.taim"
if defined TEZZ_AI_MODEL set "MODEL=%TEZZ_AI_MODEL%"
set "CODEBOOK=TezzAi\data\codebook.tnxb"
if defined TEZZ_AI_CODEBOOK set "CODEBOOK=%TEZZ_AI_CODEBOOK%"

echo [TezzAi] production training start
echo   data:     %DATA%
echo   model:    %MODEL%
echo   codebook: %CODEBOOK%

call "%TEZZ%" ai production --data "%DATA%" --model "%MODEL%" --codebook "%CODEBOOK%"
if errorlevel 1 goto :fail
call "%TEZZ%" ai stats --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail

echo [TezzAi] production training complete
echo Ask anytime:
echo   "%TEZZ%" ai ask "build api websocket route with auth middleware" --model "%MODEL%" --codebook "%CODEBOOK%" --v2
popd >nul
exit /b 0

:fail
echo [TezzAi] training failed
popd >nul
exit /b 1
