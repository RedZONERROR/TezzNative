@echo off
setlocal EnableExtensions

set "ROOT=%~dp0..\.."
pushd "%ROOT%" >nul

set "TEZZ=%ROOT%\tezz.cmd"
if defined TEZZ_CMD set "TEZZ=%TEZZ_CMD%"

set "OUT=build\ai_local"
if defined TEZZ_AI_OUT set "OUT=%TEZZ_AI_OUT%"
set "DATA=%OUT%\samples.tnxb"
set "MODEL=%OUT%\model.taim"
set "CODEBOOK=%OUT%\codebook.tnxb"

mkdir "%OUT%" 2>nul

echo [TezzAi] seed local data start
echo   out:      %OUT%
echo   data:     %DATA%
echo   model:    %MODEL%
echo   codebook: %CODEBOOK%

call "%TEZZ%" ai learn-corpus --root examples --root lib --root TezzAi --root tools --data "%DATA%" --model "%MODEL%" --codebook "%CODEBOOK%" --no-train
if errorlevel 1 goto :fail
if exist "TezzAi\data\codebook.tnxb" if /I not "%CODEBOOK%"=="TezzAi\data\codebook.tnxb" type "TezzAi\data\codebook.tnxb" >> "%CODEBOOK%"

call "%TEZZ%" ai autolearn api "build api health route with auth middleware" --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail
call "%TEZZ%" ai autolearn api "build api websocket endpoint with token auth" --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail
call "%TEZZ%" ai autolearn api "build api crud routes for users resource" --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail
call "%TEZZ%" ai autolearn api "build api request validation and json response" --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail

call "%TEZZ%" ai autolearn cli "build cli command parser with args and help output" --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail
call "%TEZZ%" ai autolearn cli "build cli tool with subcommands and flags" --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail
call "%TEZZ%" ai autolearn cli "build cli status command with table output" --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail
call "%TEZZ%" ai autolearn cli "build cli init command with boilerplate files" --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail

call "%TEZZ%" ai autolearn lib "build tokenizer utility with safe bounds checks" --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail
call "%TEZZ%" ai autolearn lib "build lib string normalizer and slug helper" --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail
call "%TEZZ%" ai autolearn lib "build lib config parser with defaults" --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail
call "%TEZZ%" ai autolearn lib "build lib path helper with validation" --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail

call "%TEZZ%" ai autolearn service "build worker loop service with periodic heartbeat" --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail
call "%TEZZ%" ai autolearn service "build service supervisor restart policy" --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail
call "%TEZZ%" ai autolearn service "build service queue processor with retry logic" --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail
call "%TEZZ%" ai autolearn service "build service health monitor and metrics" --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail

call "%TEZZ%" ai train --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail
call "%TEZZ%" ai stats --data "%DATA%" --model "%MODEL%" --v2
if errorlevel 1 goto :fail
if not exist "%MODEL%" goto :fail_model
findstr /B /C:"version=2" "%MODEL%" >nul
if errorlevel 1 goto :fail_model
if not exist "%CODEBOOK%" goto :fail_codebook
for %%F in ("%CODEBOOK%") do if %%~zF LSS 32 goto :fail_codebook

echo [TezzAi] seed local data complete
echo Test generate:
echo   "%TEZZ%" ai code "build api websocket route with auth middleware" --model "%MODEL%" --codebook "%CODEBOOK%" --pick-debug --v2
echo Start local UI:
echo   set TEZZ_AI_MODEL=%MODEL%
echo   set TEZZ_AI_CODEBOOK=%CODEBOOK%
echo   TezzAi\scripts\serve_local.cmd
popd >nul
exit /b 0

:fail
echo [TezzAi] seed local data failed
popd >nul
exit /b 1

:fail_model
echo [TezzAi] seed local data failed: model is not v2
echo   model: %MODEL%
popd >nul
exit /b 1

:fail_codebook
echo [TezzAi] seed local data failed: codebook missing or too small
echo   codebook: %CODEBOOK%
popd >nul
exit /b 1
