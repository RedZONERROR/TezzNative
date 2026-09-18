#!/usr/bin/env pwsh
# tools/publish_to_github.ps1  —  TezzNative 2.0 Production Release Automation Script
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = Split-Path $PSScriptRoot -Parent
$WorkspaceRoot = (Get-Item $PSScriptRoot).Parent.Parent.FullName
Set-Location $Root

function say {
    param(
        [Parameter(Position=0)]
        [string]$msg = "",
        [Parameter(Position=1)]
        [string]$color = "Gray"
    )
    if ($msg -eq "") {
        Write-Host ""
    } else {
        Write-Host $msg -ForegroundColor $color
    }
}

say "===============================================" "Cyan"
say "   TezzNative 2.0 Production Publisher Script  " "Cyan"
say "===============================================" "Cyan"
say ""

# ─────────────────────────────────────────────────────────────
# Step 1: Recompile Native Compiler & Tools
# ─────────────────────────────────────────────────────────────
say "[1/5] Recompiling Compiler & CLI tools in Release mode ..." "Yellow"

$buildScript = Join-Path $PSScriptRoot "build_lang_installer.ps1"
if (Test-Path $buildScript) {
    say "      Running build_lang_installer.ps1 to build TezzNativeSetup.exe..." "White"
    & powershell -ExecutionPolicy Bypass -File $buildScript
    if ($LASTEXITCODE -ne 0) { throw "SDK build failed." }
} else {
    throw "build_lang_installer.ps1 not found."
}
say "      [OK] Recompiled compiler, CLI wrapper, and setup installer." "Green"

# ─────────────────────────────────────────────────────────────
# Step 2: Run Conformance Test Suite (Language Verification)
# ─────────────────────────────────────────────────────────────
say "[2/5] Verification of full language capability lane..." "Yellow"

say "      Running Smoke Tests..." "White"
& powershell -ExecutionPolicy Bypass -Command ".\tezz.cmd test --smoke"
if ($LASTEXITCODE -ne 0) { throw "Smoke tests failed." }

say "      Running Conformance Tests..." "White"
& powershell -ExecutionPolicy Bypass -Command ".\tezz.cmd test --conformance"
if ($LASTEXITCODE -ne 0) { throw "Conformance tests failed." }

say "      Running Stdlib V1 Tests..." "White"
& powershell -ExecutionPolicy Bypass -Command ".\tezz.cmd test --stdlib-v1"
if ($LASTEXITCODE -ne 0) { throw "Stdlib V1 tests failed." }

say "      Running GUI Layout Tests..." "White"
& powershell -ExecutionPolicy Bypass -Command ".\tezz.cmd test --gui"
if ($LASTEXITCODE -ne 0) { throw "GUI tests failed." }

say "      Running Color Styling Tests (Phase 6)..." "White"
& powershell -ExecutionPolicy Bypass -Command ".\tezz.cmd run tests/test_color.tn"
if ($LASTEXITCODE -ne 0) { throw "Color tests failed." }

say "      Running AI/ML & LLM Inference Tests (Phase 7)..." "White"
& powershell -ExecutionPolicy Bypass -Command ".\tezz.cmd run tests/test_llm.tn"
if ($LASTEXITCODE -ne 0) { throw "LLM tests failed." }

say "      Running Voice TTS/STT Engine Tests (Phase 8)..." "White"
& powershell -ExecutionPolicy Bypass -Command ".\tezz.cmd run tests/test_tts_stt.tn"
if ($LASTEXITCODE -ne 0) { throw "TTS/STT tests failed." }

say "      [OK] All conformance lanes passed! Language capability verified." "Green"

# ─────────────────────────────────────────────────────────────
# Step 3: Package VS Code Language Extension
# ─────────────────────────────────────────────────────────────
say "[3/5] Packaging VS Code Extension with TezzCorp logo ..." "Yellow"
$extSrc = Join-Path $WorkspaceRoot "vscode-extension"
$extDest = Join-Path $Root "dist\TezzNative-SDK-1.0.0\vscode-extension"

if (Test-Path $extSrc) {
    New-Item -ItemType Directory -Path $extDest -Force | Out-Null
    Copy-Item -Path "$extSrc\*" -Destination $extDest -Recurse -Force
    say "      [OK] VS Code Extension successfully packaged in release bundle." "Green"
} else {
    say "      [Warning] vscode-extension folder not found!" "Yellow"
}

# ─────────────────────────────────────────────────────────────
# Step 4: Assemble Production Release Release-Dist
# ─────────────────────────────────────────────────────────────
say "[4/5] Assembling final production distribution assets..." "Yellow"
$ReleaseDist = Join-Path $Root "dist\TezzNative-Release-2.0.0"
New-Item -ItemType Directory -Path $ReleaseDist -Force | Out-Null

# Copy self-contained installer
Copy-Item (Join-Path $Root "TezzNativeSetup.exe") $ReleaseDist -Force

# Copy direct CLI and Compiler binaries
New-Item -ItemType Directory -Path "$ReleaseDist\bin" -Force | Out-Null
Copy-Item (Join-Path $Root "bin\Release\tezzc.exe") "$ReleaseDist\bin\tezzc.exe" -Force
Copy-Item (Join-Path $Root "bin\Release\tezz.exe")  "$ReleaseDist\bin\tezz.exe"  -Force

# Copy entire standard library
New-Item -ItemType Directory -Path "$ReleaseDist\lib" -Force | Out-Null
Copy-Item (Join-Path $Root "lib\*") "$ReleaseDist\lib\" -Recurse -Force

# Copy VS Code Extension
Copy-Item $extDest "$ReleaseDist\tezznative-vscode" -Recurse -Force

# Create complete release documentation files
$docPath = "$ReleaseDist\docs"
New-Item -ItemType Directory -Path $docPath -Force | Out-Null

$licFile = Join-Path $Root "LICENSE.txt"
if (Test-Path $licFile) { Copy-Item $licFile $ReleaseDist -Force }

say "      [OK] Production Release assets successfully compiled and assembled." "Green"

# ─────────────────────────────────────────────────────────────
# Step 5: Publish Release Instructions
# ─────────────────────────────────────────────────────────────
say "[5/5] Launch Ready! Release folder created at $ReleaseDist" "Cyan"
say ""
say "To publish to GitHub, follow these instructions:" "White"
say "1. git init / git add / git commit" "White"
say "2. Create a tag: git tag -a v2.0.0 -m 'TezzNative 2.0 Release'" "White"
say "3. Push tags: git push origin v2.0.0" "White"
say "4. Create GitHub Release, attach the following assets from:" "White"
say "     - dist/TezzNative-Release-2.0.0/TezzNativeSetup.exe (Self-contained Windows SDK Installer)" "Cyan"
say "     - dist/TezzNative-Release-2.0.0/tezznative-vscode (VS Code extension with TezzCorp logo)" "Cyan"
say "     - dist/TezzNative-Release-2.0.0/bin (Direct native compilation binaries)" "Cyan"
say ""
say "===============================================" "Green"
say "   TezzNative 2.0 successfully built & ready   " "Green"
say "===============================================" "Green"
say ""
