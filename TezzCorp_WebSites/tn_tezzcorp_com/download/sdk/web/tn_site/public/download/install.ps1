param(
  [string]$Mode = "update"
)

$ErrorActionPreference = "Stop"
$baseUrl = if ($env:TEZZ_INSTALL_BASE_URL) { $env:TEZZ_INSTALL_BASE_URL } else { "https://tn.tezzcorp.com/download" }
$installRoot = if ($env:TEZZ_INSTALL_ROOT) { $env:TEZZ_INSTALL_ROOT } else { Join-Path $env:LOCALAPPDATA "TezzNative" }
$binDir = if ($env:TEZZ_INSTALL_BIN_DIR) { $env:TEZZ_INSTALL_BIN_DIR } else { Join-Path $env:LOCALAPPDATA "Microsoft\WindowsApps" }
$archive = "tezznative-sdk.zip"

if ($Mode -eq "check") {
  Write-Host "install check: base=$baseUrl archive=$archive"
  exit 0
}

if ($Mode -eq "uninstall") {
  Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $installRoot "current")
  Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $binDir "tezz.cmd")
  Write-Host "tezz uninstall complete"
  exit 0
}

New-Item -ItemType Directory -Force -Path $installRoot | Out-Null
New-Item -ItemType Directory -Force -Path $binDir | Out-Null

$tmp = Join-Path $env:TEMP ("tezz-install-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$archivePath = Join-Path $tmp $archive
$shaPath = "$archivePath.sha256"

Invoke-WebRequest -UseBasicParsing -Uri "$baseUrl/$archive" -OutFile $archivePath
Invoke-WebRequest -UseBasicParsing -Uri "$baseUrl/$archive.sha256" -OutFile $shaPath

$expected = (Get-Content $shaPath | Select-Object -First 1).Split(' ')[0].Trim()
$actual = (Get-FileHash -Path $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
if (-not $expected -or $expected.ToLowerInvariant() -ne $actual) {
  throw "install.ps1: checksum verification failed"
}

if ($env:TEZZ_INSTALL_SIGNATURE_REQUIRED -eq "1") {
  $sigPath = "$archivePath.sig"
  Invoke-WebRequest -UseBasicParsing -Uri "$baseUrl/$archive.sig" -OutFile $sigPath
  $pubKeyPath = if ($env:TEZZ_INSTALL_PUBKEY_PEM) { $env:TEZZ_INSTALL_PUBKEY_PEM } else { Join-Path $tmp "tezz_release_pub.pem" }
  if (-not (Test-Path $pubKeyPath)) {
    Invoke-WebRequest -UseBasicParsing -Uri "$baseUrl/tezz_release_pub.pem" -OutFile $pubKeyPath
  }
  $openssl = Get-Command openssl -ErrorAction SilentlyContinue
  if (-not $openssl) {
    throw "install.ps1: signature verification requires openssl (set TEZZ_INSTALL_SIGNATURE_REQUIRED=0 to skip)"
  }
  & $openssl.Source dgst -sha256 -verify $pubKeyPath -signature $sigPath $archivePath | Out-Null
  if ($LASTEXITCODE -ne 0) {
    throw "install.ps1: signature verification failed"
  }
}

$backup = $null
$current = Join-Path $installRoot "current"
if (Test-Path $current) {
  $backup = Join-Path $installRoot ("backup." + [DateTimeOffset]::UtcNow.ToUnixTimeSeconds())
  Move-Item -Force $current $backup
}

$stage = Join-Path $tmp "stage"
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Expand-Archive -Path $archivePath -DestinationPath $stage -Force

$newRoot = if (Test-Path (Join-Path $stage "TezzNative-language")) { Join-Path $stage "TezzNative-language" } else { $stage }
if (-not (Test-Path (Join-Path $newRoot "tezz.cmd"))) {
  if ($backup -and (Test-Path $backup)) { Move-Item -Force $backup $current }
  throw "install.ps1: archive missing tezz.cmd"
}

if (Test-Path $current) { Remove-Item -Recurse -Force $current }
Copy-Item -Recurse -Force $newRoot $current

$shim = Join-Path $binDir "tezz.cmd"
"@echo off`r`n\"$current\\tezz.cmd\" %*`r`n" | Set-Content -Path $shim -Encoding ASCII

$pathUser = [Environment]::GetEnvironmentVariable("Path", "User")
if (-not $pathUser) { $pathUser = "" }
if ($pathUser -notlike "*$binDir*") {
  [Environment]::SetEnvironmentVariable("Path", ($pathUser.TrimEnd(';') + ";" + $binDir), "User")
}

Write-Host "tezz install complete: $current"
