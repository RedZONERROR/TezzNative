param([string]$Mode = "install")
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $scriptDir "install.ps1") -Mode $Mode
exit $LASTEXITCODE
