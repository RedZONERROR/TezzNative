param(
  [ValidateSet("install", "update", "reinstall", "uninstall", "check")]
  [string]$Mode = "install"
)

$ErrorActionPreference = "Stop"

$BaseUrl = if ($env:TEZZ_INSTALL_BASE) { $env:TEZZ_INSTALL_BASE } else { "https://tezznative.org/download" }
$PortalUrl = if ($env:TEZZ_PORTAL_BASE) { $env:TEZZ_PORTAL_BASE.TrimEnd("/") } else { "https://tezznative.org" }
$InstallScope = if ($env:TEZZ_INSTALL_SCOPE) { $env:TEZZ_INSTALL_SCOPE.ToLowerInvariant() } else { "user" }
if ($InstallScope -ne "user" -and $InstallScope -ne "system") { $InstallScope = "user" }

$DefaultInstallDir = if ($InstallScope -eq "system") { "C:\\ProgramData\\TezzNative" } else { Join-Path $HOME "TezzNative" }
$InstallDir = if ($env:TEZZ_INSTALL_DIR) { $env:TEZZ_INSTALL_DIR } else { $DefaultInstallDir }
$SdkDir = Join-Path $InstallDir "sdk"
$ZipPath = Join-Path $env:TEMP ("tezznative-sdk-" + [guid]::NewGuid().ToString() + ".zip")
$CacheBust = if ($env:TEZZ_INSTALL_CACHE_BUST) { $env:TEZZ_INSTALL_CACHE_BUST } else { [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds().ToString() }
$PathMode = if ($env:TEZZ_INSTALL_AUTO_PATH) { $env:TEZZ_INSTALL_AUTO_PATH.ToLowerInvariant() } else { "user" }
if ($PathMode -eq "auto") { $PathMode = if ($InstallScope -eq "system") { "system" } else { "user" } }
if ($PathMode -ne "user" -and $PathMode -ne "system" -and $PathMode -ne "none") { $PathMode = "user" }

function Test-IsAdmin {
  $id = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = New-Object Security.Principal.WindowsPrincipal($id)
  return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-RemoteVersion {
  try {
    $local = Get-LocalVersion
    $uri = "$PortalUrl/api/update_check.php?platform=windows-x64&version=$([uri]::EscapeDataString($local))&mode=$([uri]::EscapeDataString($Mode))"
    $json = Invoke-RestMethod -Method Get -Uri $uri
    if ($json -and $json.data -and $json.data.latest -and $json.data.latest.version) {
      return [string]$json.data.latest.version
    }
  } catch {
  }
  try {
    $json = Invoke-RestMethod -Method Get -Uri "$BaseUrl/sdk/version.json"
    if ($json -and $json.version) { return [string]$json.version }
  } catch {
  }
  return ""
}

function Send-InstallEvent {
  param(
    [string]$Status,
    [string]$Message
  )
  try {
    $installId = if ($env:COMPUTERNAME) { $env:COMPUTERNAME } else { [guid]::NewGuid().ToString() }
    Invoke-RestMethod -Method Post -Uri "$PortalUrl/api/install_event.php" -Body @{
      platform = "windows-x64"
      version = $(if ($RemoteVersion) { $RemoteVersion } else { "unknown" })
      status = $Status
      install_id = $installId
      message = $Message
    } | Out-Null
  } catch {
  }
}

function Get-LocalVersion {
  $verPath = Join-Path $SdkDir "version.json"
  if (Test-Path $verPath) {
    try {
      $obj = Get-Content $verPath -Raw | ConvertFrom-Json
      if ($obj -and $obj.version) { return [string]$obj.version }
    } catch {
    }
  }
  $modPath = Join-Path $SdkDir "tezz.mod"
  if (Test-Path $modPath) {
    $line = Get-Content $modPath | Where-Object { $_ -match '^\s*version\s*=' } | Select-Object -First 1
    if ($line) { return ($line -replace '^\s*version\s*=\s*', '').Trim() }
  }
  return ""
}

function Add-PathIfMissing {
  param(
    [string]$Scope,
    [string]$PathValue
  )
  $current = [Environment]::GetEnvironmentVariable("Path", $Scope)
  if (-not $current) { $current = "" }
  $parts = $current.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries)
  foreach ($part in $parts) {
    if ($part.TrimEnd('\\') -ieq $PathValue.TrimEnd('\\')) {
      return
    }
  }
  $next = if ([string]::IsNullOrWhiteSpace($current)) { $PathValue } else { "$current;$PathValue" }
  [Environment]::SetEnvironmentVariable("Path", $next, $Scope)
}

function Configure-Env {
  param(
    [string]$BinDir,
    [string]$SdkRoot
  )
  switch ($PathMode) {
    "system" {
      if (Test-IsAdmin) {
        Add-PathIfMissing -Scope "Machine" -PathValue $BinDir
        [Environment]::SetEnvironmentVariable("TEZZ_SDK_ROOT", $SdkRoot, "Machine")
      } else {
        Write-Host "Admin rights not available. Falling back to user PATH/env."
        Add-PathIfMissing -Scope "User" -PathValue $BinDir
        [Environment]::SetEnvironmentVariable("TEZZ_SDK_ROOT", $SdkRoot, "User")
      }
    }
    "none" {
      [Environment]::SetEnvironmentVariable("TEZZ_SDK_ROOT", $SdkRoot, "User")
    }
    default {
      Add-PathIfMissing -Scope "User" -PathValue $BinDir
      [Environment]::SetEnvironmentVariable("TEZZ_SDK_ROOT", $SdkRoot, "User")
    }
  }
}

function Remove-Install {
  if (Test-Path $InstallDir) {
    Remove-Item -Recurse -Force $InstallDir
  }
  $userBin = Join-Path $HOME "bin"
  Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $userBin "tezz.cmd")
  Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $userBin "tezz.ps1")
  Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $userBin "tezzc.cmd")
  $sysBin = Join-Path $InstallDir "bin"
  Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $sysBin "tezz.cmd")
  Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $sysBin "tezz.ps1")
  Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $sysBin "tezzc.cmd")
  Remove-Item -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\TezzNative" -Recurse -Force -ErrorAction SilentlyContinue
  Remove-Item -Path "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\TezzNative" -Recurse -Force -ErrorAction SilentlyContinue
}

function Normalize-SdkLayout {
  $binDir = Join-Path $SdkDir "bin"
  if (-not (Test-Path $binDir)) {
    New-Item -ItemType Directory -Force -Path $binDir | Out-Null
  }

  $preferred = Join-Path $binDir "tezzc-windows-x64.exe"
  $armPreferred = Join-Path $binDir "tezzc-windows-arm64.exe"
  $fromBin = Join-Path $binDir "tezzc.exe"
  $fromBuild = Join-Path (Join-Path $SdkDir "build") "tezzc.exe"
  $fromRoot = Join-Path $SdkDir "tezzc.exe"

  if (($env:PROCESSOR_ARCHITECTURE -eq "ARM64") -and (Test-Path $armPreferred)) {
    Copy-Item -Force $armPreferred $fromBin
  } elseif (Test-Path $preferred) {
    Copy-Item -Force $preferred $fromBin
  } elseif (Test-Path $fromBuild) {
    Copy-Item -Force $fromBuild $fromBin
  } elseif (Test-Path $fromRoot) {
    Copy-Item -Force $fromRoot $fromBin
  }

  if (-not (Test-Path $preferred) -and (Test-Path $fromBin)) {
    Copy-Item -Force $fromBin $preferred
  }

  # Ensure root launchers and bin launchers are synchronized
  foreach ($launcher in @("tezz.cmd", "tezz.ps1", "tezz")) {
    $rootLauncher = Join-Path $SdkDir $launcher
    $binLauncher = Join-Path $binDir $launcher
    if (-not (Test-Path $rootLauncher) -and (Test-Path $binLauncher)) {
      Copy-Item -Force $binLauncher $rootLauncher
    }
    if (-not (Test-Path $binLauncher) -and (Test-Path $rootLauncher)) {
      Copy-Item -Force $rootLauncher $binLauncher
    }
  }
}

function Assert-SdkIntegrity {
  $required = @(
    (Join-Path $SdkDir "tezz.cmd"),
    (Join-Path $SdkDir "tezz.ps1"),
    (Join-Path $SdkDir "tezz.mod"),
    (Join-Path (Join-Path $SdkDir "tools") "tezz.tn"),
    (Join-Path (Join-Path $SdkDir "lib") "std.tn"),
    (Join-Path (Join-Path $SdkDir "lib") "io.tn"),
    (Join-Path (Join-Path $SdkDir "bin") "tezzc-windows-x64.exe")
  )
  $missing = @()
  foreach ($item in $required) {
    if (-not (Test-Path $item)) { $missing += $item }
  }
  if ($missing.Count -gt 0) {
    throw "SDK integrity check failed. Missing: $($missing -join ', ')"
  }
}

function Install-Shims {
  $binDir = if ($InstallScope -eq "system") { Join-Path $InstallDir "bin" } else { Join-Path $HOME "bin" }
  if (-not (Test-Path $binDir)) {
    New-Item -ItemType Directory -Force -Path $binDir | Out-Null
  }

  $tezzcExe = Join-Path (Join-Path $SdkDir "bin") "tezzc-windows-x64.exe"
  if (($env:PROCESSOR_ARCHITECTURE -eq "ARM64") -and (Test-Path (Join-Path (Join-Path $SdkDir "bin") "tezzc-windows-arm64.exe"))) {
    $tezzcExe = Join-Path (Join-Path $SdkDir "bin") "tezzc-windows-arm64.exe"
  } elseif (Test-Path (Join-Path (Join-Path $SdkDir "bin") "tezzc.exe")) {
    $tezzcExe = Join-Path (Join-Path $SdkDir "bin") "tezzc.exe"
  }

  $cmdShimPath = Join-Path $binDir "tezz.cmd"
  $psShimPath = Join-Path $binDir "tezz.ps1"
  $tezzcShimPath = Join-Path $binDir "tezzc.cmd"

  $cmdShim = "@echo off`r`nset `"TEZZ_SDK_ROOT=$SdkDir`"`r`nset `"TEZZC=$tezzcExe`"`r`n`"$SdkDir\tezz.cmd`" %*`r`n"
  $psShim = "`$env:TEZZ_SDK_ROOT = `"$SdkDir`"`r`n`$env:TEZZC = `"$tezzcExe`"`r`n& `"$SdkDir\tezz.ps1`" @args`r`n"
  $tezzcShim = "@echo off`r`n`"$tezzcExe`" %*`r`n"

  Set-Content -Path $cmdShimPath -Value $cmdShim -Encoding ASCII
  Set-Content -Path $psShimPath -Value $psShim -Encoding ASCII
  Set-Content -Path $tezzcShimPath -Value $tezzcShim -Encoding ASCII

  Configure-Env -BinDir $binDir -SdkRoot $SdkDir
  return $binDir
}

function Smoke-TestInstall {
  param([string]$BinDir)
  $tezzc = Join-Path $SdkDir "bin\tezzc-windows-x64.exe"
  if (($env:PROCESSOR_ARCHITECTURE -eq "ARM64") -and (Test-Path (Join-Path $SdkDir "bin\tezzc-windows-arm64.exe"))) {
    $tezzc = Join-Path $SdkDir "bin\tezzc-windows-arm64.exe"
  } elseif (Test-Path (Join-Path $SdkDir "bin\tezzc.exe")) {
    $tezzc = Join-Path $SdkDir "bin\tezzc.exe"
  }

  $probe = Join-Path $env:TEMP ("tezznative_smoke_" + [guid]::NewGuid().ToString() + ".tn")
  $probeExe = Join-Path $env:TEMP ("tezznative_smoke_" + [guid]::NewGuid().ToString() + ".exe")
  @"
fn main() -> int:
  say "tezz-smoke-ok"
  ret 0
"@ | Set-Content -Path $probe -Encoding ASCII

  $env:PATH = "$SdkDir\bin;$SdkDir\lib;$env:PATH"
  & $tezzc buildexe $probe $probeExe | Out-Null
  $buildOk = (Test-Path $probeExe)
  Remove-Item -Force -ErrorAction SilentlyContinue $probe, $probeExe

  if (-not $buildOk) {
    throw "Smoke test failed: tezzc buildexe"
  }
}

function Get-ExpectedSha256 {
  param([string]$FileName)

  $shaPath = Join-Path $env:TEMP ("$FileName-" + [guid]::NewGuid().ToString() + ".sha256")
  try {
    Invoke-WebRequest -Uri "$BaseUrl/$FileName.sha256?nocache=$CacheBust" -OutFile $shaPath
    $raw = (Get-Content -Path $shaPath -Raw).Trim()
    $parts = $raw -split '\s+'
    if ($parts.Count -lt 1 -or $parts[0] -notmatch '^[0-9A-Fa-f]{64}$') {
      throw "Invalid checksum file for $FileName"
    }
    return $parts[0].ToUpperInvariant()
  } finally {
    Remove-Item -Force -ErrorAction SilentlyContinue $shaPath
  }
}

function Assert-ArchiveChecksum {
  param(
    [string]$Path,
    [string]$FileName
  )

  $expected = Get-ExpectedSha256 -FileName $FileName
  $actual = (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToUpperInvariant()
  if ($actual -ne $expected) {
    throw "Checksum mismatch for $FileName. Expected $expected but got $actual. Install stopped before extraction."
  }
  Write-Host "Verified SHA-256: $actual"
}

function Install-Payload {
  param([string]$RemoteVersion)

  if ($InstallScope -eq "system" -and -not (Test-IsAdmin)) {
    throw "System install requires Administrator PowerShell. Re-run as admin or set TEZZ_INSTALL_SCOPE=user."
  }

  Write-Host "Downloading TezzNative SDK..."
  Invoke-WebRequest -Uri "$BaseUrl/tezznative-sdk.zip?nocache=$CacheBust" -OutFile $ZipPath
  Assert-ArchiveChecksum -Path $ZipPath -FileName "tezznative-sdk.zip"

  if (Test-Path $InstallDir) {
    Remove-Item -Recurse -Force $InstallDir
  }
  New-Item -ItemType Directory -Force -Path $SdkDir | Out-Null
  Expand-Archive -Path $ZipPath -DestinationPath $SdkDir -Force
  Remove-Item -Force $ZipPath

  Normalize-SdkLayout
  Assert-SdkIntegrity
  $binDir = Install-Shims
  Smoke-TestInstall -BinDir $binDir

  # Register in Windows Control Panel Programs (Installed Apps)
  try {
    $uninstallKey = if ($InstallScope -eq "system" -and (Test-IsAdmin)) { "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\TezzNative" } else { "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\TezzNative" }
    if (-not (Test-Path $uninstallKey)) {
      New-Item -Path $uninstallKey -Force | Out-Null
    }
    Set-ItemProperty -Path $uninstallKey -Name "DisplayName" -Value "TezzNative Programming Language SDK v2.2.1"
    Set-ItemProperty -Path $uninstallKey -Name "DisplayVersion" -Value "2.2.1"
    Set-ItemProperty -Path $uninstallKey -Name "Publisher" -Value "TezzCorp Pvt Ltd. (Created by Rohit Pathak)"
    Set-ItemProperty -Path $uninstallKey -Name "InstallLocation" -Value $InstallDir
    Set-ItemProperty -Path $uninstallKey -Name "DisplayIcon" -Value "$binDir\tezz.cmd,0"
    Set-ItemProperty -Path $uninstallKey -Name "UninstallString" -Value "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command `"`$env:TEZZ_INSTALL_DIR='$InstallDir'; irm $BaseUrl/install.ps1 | iex; Remove-Install`""
    Set-ItemProperty -Path $uninstallKey -Name "URLInfoAbout" -Value "https://tezznative.org"
    Set-ItemProperty -Path $uninstallKey -Name "HelpLink" -Value "https://tezznative.org/docs/"
    Set-ItemProperty -Path $uninstallKey -Name "EstimatedSize" -Value 120000 -Type DWord
    Set-ItemProperty -Path $uninstallKey -Name "NoModify" -Value 1 -Type DWord
    Set-ItemProperty -Path $uninstallKey -Name "NoRepair" -Value 1 -Type DWord
  } catch {
  }

  Write-Host ""
  Write-Host "TezzNative installed successfully." -ForegroundColor Green
  Write-Host "Created by Rohit Pathak | TezzCorp Pvt Ltd." -ForegroundColor Cyan
  Write-Host "Install root: $InstallDir"
  Write-Host "SDK root:     $SdkDir"
  Write-Host "Bin dir:      $binDir"
  if ($RemoteVersion) {
    Write-Host "Version:      $RemoteVersion"
  }
  Write-Host ""
  Write-Host "Next steps:"
  Write-Host "  tezz --version"
  Write-Host "  tezz init my-project"
  Write-Host "  tezz run hello.tn"
  Write-Host ""
  Write-Host "Direct Script Execution:"
  Write-Host "  1. Add shebang: #!/usr/bin/env tezz"
  Write-Host "  2. Run from WSL/Git Bash or call: tezz script.tn"
}

$Mode = $Mode.ToLowerInvariant()
$RemoteVersion = Get-RemoteVersion
$LocalVersion = Get-LocalVersion

switch ($Mode) {
  "check" {
    Write-Host "Local version:  " $(if ($LocalVersion) { $LocalVersion } else { "not-installed" })
    Write-Host "Remote version: " $(if ($RemoteVersion) { $RemoteVersion } else { "unknown" })
    if ($LocalVersion -and $RemoteVersion -and $LocalVersion -eq $RemoteVersion) {
      Write-Host "Status: up-to-date"
    } elseif ($LocalVersion) {
      Write-Host "Status: update-available"
    } else {
      Write-Host "Status: not-installed"
    }
    Send-InstallEvent -Status "check" -Message "local=$LocalVersion remote=$RemoteVersion"
  }
  "uninstall" {
    Remove-Install
    Write-Host "TezzNative removed from $InstallDir"
    Send-InstallEvent -Status "uninstall" -Message "removed"
  }
  "reinstall" {
    Remove-Install
    Install-Payload -RemoteVersion $RemoteVersion
    Send-InstallEvent -Status "reinstall" -Message "completed"
  }
  "update" {
    if (-not $LocalVersion) {
      Install-Payload -RemoteVersion $RemoteVersion
      Send-InstallEvent -Status "install" -Message "installed from update mode"
    } elseif ($RemoteVersion -and $LocalVersion -eq $RemoteVersion) {
      Write-Host "TezzNative is already up-to-date ($LocalVersion)."
      Send-InstallEvent -Status "check" -Message "already up-to-date"
    } else {
      Install-Payload -RemoteVersion $RemoteVersion
      Send-InstallEvent -Status "update" -Message "updated"
    }
  }
  default {
    Install-Payload -RemoteVersion $RemoteVersion
    Send-InstallEvent -Status "install" -Message "completed"
  }
}
