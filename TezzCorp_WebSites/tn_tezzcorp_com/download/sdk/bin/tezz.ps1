$ErrorActionPreference = "Stop"
$bin = (Split-Path -Parent $MyInvocation.MyCommand.Path).TrimEnd('\','/')
$root = Split-Path -Parent $bin
$launcher = Join-Path $root "tezz.ps1"

if (-not (Test-Path $launcher)) {
  Write-Error "bin\\tezz.ps1: missing $launcher"
  exit 1
}

& powershell -ExecutionPolicy Bypass -File $launcher @args
exit $LASTEXITCODE
