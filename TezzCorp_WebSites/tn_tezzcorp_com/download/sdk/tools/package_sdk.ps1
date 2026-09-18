$ErrorActionPreference = "Stop"
$root = (Split-Path -Parent $MyInvocation.MyCommand.Path | Split-Path -Parent)
Set-Location $root

$compiler = Join-Path $root "bin\tezzc.exe"
if (-not (Test-Path $compiler)) {
  $compiler = Join-Path $root "build\tezzc.exe"
}
if (-not (Test-Path $compiler)) {
  throw "package_sdk: missing bin\tezzc.exe or build\tezzc.exe (run tools\build_core_strict.ps1 first)"
}

$archive = Join-Path $root "build\tezznative-sdk.zip"
$tmp = Join-Path $env:TEMP ("tezz-sdk-" + [guid]::NewGuid().ToString("N"))
$stage = Join-Path $tmp "TezzNative-language"
New-Item -ItemType Directory -Force -Path (Join-Path $stage "build") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage "bin") | Out-Null

$items = @(
  "CMakeLists.txt",
  "README.md",
  "SECURITY.md",
  "benchmarks",
  "docs",
  "examples",
  "include",
  "lib",
  "modules",
  "projects",
  "runtime",
  "src",
  "tests",
  "tezznative-vscode",
  "tools",
  "web",
  "tezz",
  "tezz.cmd",
  "tezz.ps1",
  "tezz.mod",
  "tezz.lock",
  "registry.tnx",
  "version.json"
)

foreach ($item in $items) {
  Copy-Item -Recurse -Force $item $stage
}
Copy-Item -Force $compiler (Join-Path $stage "build\tezzc.exe")
Copy-Item -Force $compiler (Join-Path $stage "bin\tezzc.exe")
Copy-Item -Force $compiler (Join-Path $stage "bin\tezzc-windows-x64.exe")
$linuxCompiler = Join-Path $root "bin\tezzc-linux-x64"
if (Test-Path $linuxCompiler) {
  Copy-Item -Force $linuxCompiler (Join-Path $stage "bin\tezzc-linux-x64")
}
foreach ($launcher in @("bin\\tezz", "bin\\tezz.cmd", "bin\\tezz.ps1")) {
  if (Test-Path $launcher) {
    Copy-Item -Force $launcher (Join-Path $stage "bin")
  }
}

if (Test-Path $archive) {
  Remove-Item -Force $archive
}
Compress-Archive -Path $stage -DestinationPath $archive
Remove-Item -Recurse -Force $tmp
Write-Host "package_sdk: wrote $archive"
