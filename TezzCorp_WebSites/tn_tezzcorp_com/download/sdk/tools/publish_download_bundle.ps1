$ErrorActionPreference = "Stop"
$root = (Split-Path -Parent $MyInvocation.MyCommand.Path | Split-Path -Parent)
Set-Location $root

$downloadDir = Join-Path $root "web\tn_site\public\download"
New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null

$sources = @(
  "build\tezznative-sdk-linux.tar.gz",
  "build\tezznative-sdk-macos.tar.gz",
  "build\tezznative-sdk.zip"
)

$copied = 0
foreach ($src in $sources) {
  if (-not (Test-Path $src)) {
    continue
  }
  $base = [System.IO.Path]::GetFileName($src)
  $dst = Join-Path $downloadDir $base
  Copy-Item -Force $src $dst
  $hash = (Get-FileHash -Path $dst -Algorithm SHA256).Hash.ToLowerInvariant()
  "$hash  $base" | Set-Content -Encoding ASCII -Path ($dst + ".sha256")
  $copied += 1
}

if (Test-Path "build\release_artifacts.tnx") {
  Copy-Item -Force "build\release_artifacts.tnx" (Join-Path $downloadDir "release_artifacts.tnx")
}
if (Test-Path "build\reproducible_build.tnx") {
  Copy-Item -Force "build\reproducible_build.tnx" (Join-Path $downloadDir "reproducible_build.tnx")
}

if ($copied -eq 0) {
  throw "publish_download_bundle.ps1: no SDK archives found under build\"
}

Write-Host "publish_download_bundle.ps1: updated $downloadDir"
