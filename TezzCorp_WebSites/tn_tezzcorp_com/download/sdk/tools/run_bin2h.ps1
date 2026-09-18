$ErrorActionPreference = "Stop"
$vsbase  = "C:\Program Files\Microsoft Visual Studio\18\Community"
$msvcVer = (Get-ChildItem "$vsbase\VC\Tools\MSVC" | Select-Object -First 1).Name
$sdkVer  = (Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\Include" | Sort-Object Name -Desc | Select-Object -First 1).Name
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

& $cl /O2 /nologo @iArgs tools\bin2h.c /Fe:tools\bin2h.exe /link /SUBSYSTEM:CONSOLE @lArgs user32.lib
if ($LASTEXITCODE -eq 0) {
    .\tools\bin2h.exe
}
