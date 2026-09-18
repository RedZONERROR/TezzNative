#!/usr/bin/env pwsh
# tools/package.ps1  —  TezzNative App Packager v2.0
# Usage: PowerShell -ExecutionPolicy Bypass -File tools/package.ps1 [-App calculator] [-Name TezzCalcPro] [-Version 1.0.0]
param(
    [string]$App     = "calculator",
    [string]$Name    = "TezzCalcPro",
    [string]$Version = "1.0.0"
)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root  = Split-Path $PSScriptRoot -Parent
$Dist  = Join-Path $Root "dist\$Name-$Version"

# ---- Find MSVC -------------------------------------------------------
$vsbase  = "C:\Program Files\Microsoft Visual Studio\18\Community"
$msvcVer = (Get-ChildItem "$vsbase\VC\Tools\MSVC" | Select-Object -First 1).Name
$sdkVer  = (Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\Include" |
             Sort-Object Name -Desc | Select-Object -First 1).Name
$cl   = "$vsbase\VC\Tools\MSVC\$msvcVer\bin\Hostx64\x64\cl.exe"
$inc  = @(
    "$vsbase\VC\Tools\MSVC\$msvcVer\include",
    "C:\Program Files (x86)\Windows Kits\10\Include\$sdkVer\ucrt",
    "C:\Program Files (x86)\Windows Kits\10\Include\$sdkVer\um",
    "C:\Program Files (x86)\Windows Kits\10\Include\$sdkVer\shared"
)
$libs = @(
    "$vsbase\VC\Tools\MSVC\$msvcVer\lib\x64",
    "C:\Program Files (x86)\Windows Kits\10\Lib\$sdkVer\ucrt\x64",
    "C:\Program Files (x86)\Windows Kits\10\Lib\$sdkVer\um\x64"
)
$iArgs = ($inc  | ForEach-Object { "/I$_" })
$lArgs = ($libs | ForEach-Object { "/LIBPATH:$_" })
$syslibs = "user32.lib","kernel32.lib","shell32.lib","ole32.lib","comctl32.lib","advapi32.lib","gdi32.lib"

Write-Host ""
Write-Host "=======================================" -ForegroundColor Cyan
Write-Host "  TezzNative Packager v2.0" -ForegroundColor Cyan
Write-Host "=======================================" -ForegroundColor Cyan
Write-Host ""

# 1. Build launcher (TezzCalc.exe)
Write-Host "[1/4] Compiling launcher -> $Name.exe ..." -ForegroundColor Yellow
$launcherSrc  = Join-Path $Root "tools\launcher.c"
$launcherOut  = Join-Path $Root "$Name.exe"
& $cl /O2 /nologo /MT @iArgs $launcherSrc /Fe:$launcherOut /link /SUBSYSTEM:WINDOWS @lArgs user32.lib kernel32.lib 2>&1 | Where-Object { $_ -match "error" }
if ($LASTEXITCODE -ne 0) { throw "Launcher compilation failed." }
Write-Host "      OK launcher" -ForegroundColor Green

# 2. Build installer (TezzCalcSetup.exe)
Write-Host "[2/4] Compiling installer -> ${Name}Setup.exe ..." -ForegroundColor Yellow
$instSrc = Join-Path $Root "tools\installer_win.c"
$instOut  = Join-Path $Root "${Name}Setup.exe"
& $cl /O2 /nologo /MT @iArgs $instSrc /Fe:$instOut /link /SUBSYSTEM:WINDOWS @lArgs @syslibs 2>&1 | Where-Object { $_ -match "error|warning" -and $_ -notmatch "C4100" }
if ($LASTEXITCODE -ne 0) { throw "Installer compilation failed." }
Write-Host "      OK installer" -ForegroundColor Green

# 3. Assemble dist package
Write-Host "[3/4] Assembling dist -> $Dist ..." -ForegroundColor Yellow
New-Item -ItemType Directory -Path $Dist -Force | Out-Null

$tezzc = Join-Path $Root "bin\Release\tezzc.exe"
$appTn = Join-Path $Root "$App.tn"
$icon  = Join-Path $Root "assets\calculator_blue.ico"
$lic   = Join-Path $Root "LICENSE.txt"

Copy-Item $launcherOut (Join-Path $Dist "$Name.exe")    -Force
Copy-Item $instOut     (Join-Path $Dist "${Name}Setup.exe") -Force
Copy-Item $tezzc       (Join-Path $Dist "tezzc.exe")    -Force
Copy-Item $appTn       (Join-Path $Dist "$App.tn")      -Force
if (Test-Path $icon) { Copy-Item $icon (Join-Path $Dist "app.ico") -Force }
if (Test-Path $lic)  { Copy-Item $lic  $Dist -Force }
Write-Host "      OK dist" -ForegroundColor Green

# 4. Copy installer to dist root for easy distribution
$instDest = Join-Path $Root "${Name}Setup.exe"
if ($instOut -ne $instDest) { Copy-Item $instOut $instDest -Force }
Write-Host "[4/4] ${Name}Setup.exe ready" -ForegroundColor Green

Write-Host ""
Write-Host "=======================================" -ForegroundColor Cyan
Write-Host "  Done!" -ForegroundColor Cyan
Write-Host "  Launcher : $launcherOut" -ForegroundColor White
Write-Host "  Setup    : $instOut" -ForegroundColor White
Write-Host "  Dist     : $Dist" -ForegroundColor White
Write-Host "=======================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "To install: run ${Name}Setup.exe" -ForegroundColor Green
Write-Host "To run direct: place $Name.exe + tezzc.exe + $App.tn in the same folder." -ForegroundColor DarkGray
