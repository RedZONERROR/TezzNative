#!/usr/bin/env pwsh
# tools/build_lang_installer.ps1  — TezzNative SDK self-contained installer + tezz CLI builder
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root    = Split-Path $PSScriptRoot -Parent
$vsbase  = "C:\Program Files\Microsoft Visual Studio\18\Community"
$msvcVer = (Get-ChildItem "$vsbase\VC\Tools\MSVC" | Select-Object -First 1).Name
$sdkVer  = (Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\Include" | Sort-Object Name -Desc | Select-Object -First 1).Name
$cl      = "$vsbase\VC\Tools\MSVC\$msvcVer\bin\Hostx64\x64\cl.exe"
$inc     = @(
    "$vsbase\VC\Tools\MSVC\$msvcVer\include",
    "C:\Program Files (x86)\Windows Kits\10\Include\$sdkVer\ucrt",
    "C:\Program Files (x86)\Windows Kits\10\Include\$sdkVer\um",
    "C:\Program Files (x86)\Windows Kits\10\Include\$sdkVer\shared"
)
$libs    = @(
    "$vsbase\VC\Tools\MSVC\$msvcVer\lib\x64",
    "C:\Program Files (x86)\Windows Kits\10\Lib\$sdkVer\ucrt\x64",
    "C:\Program Files (x86)\Windows Kits\10\Lib\$sdkVer\um\x64"
)
$iArgs   = $inc  | ForEach-Object { "/I$_" }
$lArgs   = $libs | ForEach-Object { "/LIBPATH:$_" }
$syslibs = "user32.lib","kernel32.lib","shell32.lib","ole32.lib","comctl32.lib","advapi32.lib","gdi32.lib"

Write-Host ""
Write-Host "===  TezzNative SDK Builder  ===" -ForegroundColor Cyan
Write-Host ""

# 0 — Build tezz.exe CLI
Write-Host "[1/5] Building tezz.exe CLI wrapper ..." -ForegroundColor Yellow
$cliSrc = Join-Path $PSScriptRoot "tezz_cli.c"
$cliOut = Join-Path $Root "bin\Release\tezz.exe"
& $cl /O2 /nologo /MT @iArgs $cliSrc /Fe:$cliOut /link /SUBSYSTEM:CONSOLE @lArgs user32.lib kernel32.lib 2>&1 | Where-Object { $_ -match 'error' }
if ($LASTEXITCODE -ne 0) { throw "tezz.exe CLI compilation failed." }
Write-Host "      OK  tezz.exe" -ForegroundColor Green

# 1 — Rebuild compiler
Write-Host "[2/5] Rebuilding tezzc.exe compiler ..." -ForegroundColor Yellow
Push-Location $Root
cmake --build build --config Release --target tezzc 2>&1 | Select-Object -Last 3
if ($LASTEXITCODE -ne 0) { throw "Compiler build failed." }
Pop-Location
Write-Host "      OK  tezzc.exe" -ForegroundColor Green

# 2 — Generate payload.h (embeds tezzc.exe + tezz.exe + lib/*.tn + tezz.mod/lock)
Write-Host "[3/5] Generating embedded payload ..." -ForegroundColor Yellow
Push-Location $Root
& $cl /O2 /nologo @iArgs tools\bin2h.c /Fe:tools\bin2h.exe /link /SUBSYSTEM:CONSOLE @lArgs user32.lib | Out-Null
if ($LASTEXITCODE -ne 0) { throw "bin2h compilation failed." }
.\tools\bin2h.exe
if ($LASTEXITCODE -ne 0) { throw "payload.h generation failed." }
Pop-Location
Write-Host "      OK  payload.h" -ForegroundColor Green

# 3 — Compile self-contained installer
Write-Host "[4/5] Compiling TezzNativeSetup.exe ..." -ForegroundColor Yellow
$instSrc = Join-Path $PSScriptRoot "lang_installer_win.c"
$instOut  = Join-Path $Root "TezzNativeSetup.exe"
& $cl /O2 /nologo /MT @iArgs $instSrc /Fe:$instOut `
    /link /SUBSYSTEM:WINDOWS `
    "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'" `
    @lArgs @syslibs 2>&1 | Where-Object { $_ -match 'error' }
if ($LASTEXITCODE -ne 0) { throw "Installer compilation failed." }
$sizeMB = [Math]::Round((Get-Item $instOut).Length/1MB,1)
Write-Host "      OK  TezzNativeSetup.exe  ($sizeMB MB)" -ForegroundColor Green

# 4 — Assemble dist
Write-Host "[5/5] Assembling dist ..." -ForegroundColor Yellow
$Dist = Join-Path $Root "dist\TezzNative-SDK-1.0.0"
New-Item -ItemType Directory -Path $Dist -Force | Out-Null
Copy-Item $instOut (Join-Path $Dist "TezzNativeSetup.exe") -Force
# Also copy standalone binaries for users who install manually
Copy-Item (Join-Path $Root "bin\Release\tezzc.exe") (Join-Path $Dist "tezzc.exe") -Force
Copy-Item (Join-Path $Root "bin\Release\tezz.exe")  (Join-Path $Dist "tezz.exe")  -Force
Write-Host "      OK  dist" -ForegroundColor Green

Write-Host ""
Write-Host "===  Done!  ===" -ForegroundColor Cyan
Write-Host "  Installer : $instOut" -ForegroundColor White
Write-Host "  Dist      : $Dist" -ForegroundColor White
Write-Host ""
Write-Host "Run TezzNativeSetup.exe - contains all SDK files embedded." -ForegroundColor Green
