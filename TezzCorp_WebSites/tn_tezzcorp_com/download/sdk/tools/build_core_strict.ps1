param(
  [string]$OutPath = ""
)

$ErrorActionPreference = "Stop"
$root = (Split-Path -Parent $MyInvocation.MyCommand.Path | Split-Path -Parent)
Set-Location $root
New-Item -ItemType Directory -Force -Path "build" | Out-Null
New-Item -ItemType Directory -Force -Path "bin" | Out-Null

$compiler = if ($env:CC) { $env:CC } else { $null }
if (-not $compiler) {
  foreach ($candidate in @("gcc", "clang")) {
    $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
    if ($cmd) {
      $compiler = $cmd.Source
      break
    }
  }
}
if (-not $compiler) {
  throw "build_core_strict: no supported C compiler found (set CC or install gcc/clang)"
}

$sources = Get-ChildItem "src\*.c" | ForEach-Object { $_.FullName }
if (-not $sources -or $sources.Count -eq 0) {
  throw "build_core_strict: no source files found"
}

$sdlCflags = @()
$sdlLibs = @()
$pkgConfig = Get-Command "pkg-config" -ErrorAction SilentlyContinue
if ($pkgConfig) {
  & $pkgConfig.Source --exists sdl2 2>$null
  if ($LASTEXITCODE -eq 0) {
    $cflagsOut = (& $pkgConfig.Source --cflags sdl2).Trim()
    $libsOut = (& $pkgConfig.Source --libs sdl2).Trim()
    if ($cflagsOut.Length -gt 0) {
      $sdlCflags = $cflagsOut -split "\s+" | Where-Object { $_ -and $_.Trim().Length -gt 0 }
    }
    if ($libsOut.Length -gt 0) {
      $sdlLibs = $libsOut -split "\s+" | Where-Object { $_ -and $_.Trim().Length -gt 0 }
    }
  }
}

$out = Join-Path $root "build\tezzc.exe"
if ($OutPath -and $OutPath.Trim().Length -gt 0) {
  if ([System.IO.Path]::IsPathRooted($OutPath)) {
    $out = $OutPath
  } else {
    $out = Join-Path $root $OutPath
  }
}
$outDir = Split-Path -Parent $out
if ($outDir -and -not (Test-Path $outDir)) {
  New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}
if (Test-Path $out) {
  Remove-Item -Force $out
}

$args = @(
  "-Iinclude",
  "-std=c11",
  "-O2",
  "-Wall",
  "-Wextra",
  "-Werror"
)
if ($sdlLibs.Count -gt 0) {
  $args += "-DTN_ENABLE_HOST_GUI=1"
}
$args += @(
  "-Wl,--no-insert-timestamp"
) + $sdlCflags + $sources + @(
  "-o", $out,
  "-lws2_32",
  "-lsecur32",
  "-lcrypt32",
  "-lwinhttp",
  "-luser32",
  "-lgdi32"
)
$args += $sdlLibs

& $compiler @args
if ($LASTEXITCODE -ne 0) {
  throw "build_core_strict: compiler build failed"
}

$defaultOut = Join-Path $root "build\tezzc.exe"
$isDefaultOut = ($out -ieq $defaultOut)
if ($isDefaultOut) {
  $binCanon = Join-Path $root "bin\\tezzc.exe"
  $binAlias = Join-Path $root "bin\\tezzc-windows-x64.exe"
  Copy-Item -Force $out $binCanon
  Copy-Item -Force $out $binAlias
}

if ($sdlLibs.Count -gt 0) {
  Write-Host "build_core_strict: built $out with $compiler (host GUI backend enabled)"
} else {
  Write-Host "build_core_strict: built $out with $compiler"
}
