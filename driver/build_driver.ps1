$ErrorActionPreference = "Stop"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe was not found. Install Visual Studio Build Tools with MSVC."
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsPath) {
    throw "Visual Studio with MSBuild/MSVC was not found."
}

$msvcRoot = Join-Path $vsPath "VC\Tools\MSVC"
$msvc = Get-ChildItem -Directory $msvcRoot | Sort-Object Name -Descending | Select-Object -First 1
if (-not $msvc) {
    throw "MSVC toolchain was not found under $msvcRoot."
}

$cl = Join-Path $msvc.FullName "bin\HostX86\x64\cl.exe"
$link = Join-Path $msvc.FullName "bin\HostX86\x64\link.exe"
$msvcInclude = Join-Path $msvc.FullName "include"
if (-not (Test-Path $cl) -or -not (Test-Path $link)) {
    throw "x64 MSVC compiler/linker was not found."
}

$kitsInclude = "${env:ProgramFiles(x86)}\Windows Kits\10\Include"
$kitRoot = Split-Path $kitsInclude -Parent
$kit = Get-ChildItem -Directory $kitsInclude |
    Where-Object {
        (Test-Path (Join-Path $_.FullName "km\ntifs.h")) -and
        (Test-Path (Join-Path $kitRoot "Lib\$($_.Name)\km\x64\ntoskrnl.lib"))
    } |
    Sort-Object Name -Descending |
    Select-Object -First 1
if (-not $kit) {
    throw "Windows Driver Kit kernel headers/libs were not found. Install the Windows 10/11 WDK."
}

$kitVersion = $kit.Name
$kmInclude = Join-Path $kitsInclude "$kitVersion\km"
$kmLib = Join-Path $kitRoot "Lib\$kitVersion\km\x64"
$sharedInclude = Get-ChildItem -Directory $kitsInclude |
    Where-Object { Test-Path (Join-Path $_.FullName "shared\ntdef.h") } |
    Sort-Object Name -Descending |
    Select-Object -First 1
if (-not $sharedInclude) {
    throw "Windows SDK shared headers were not found. Missing ntdef.h."
}

$sharedIncludePath = Join-Path $sharedInclude.FullName "shared"
$ucrtInclude = Get-ChildItem -Directory $kitsInclude |
    Where-Object { Test-Path (Join-Path $_.FullName "ucrt") } |
    Sort-Object Name -Descending |
    Select-Object -First 1
$ucrtIncludePath = if ($ucrtInclude) { Join-Path $ucrtInclude.FullName "ucrt" } else { $null }
$outDir = Join-Path $PSScriptRoot "x64\Release"
$objDir = Join-Path $PSScriptRoot "obj\x64\Release"
New-Item -ItemType Directory -Force -Path $outDir, $objDir | Out-Null

$src = Join-Path $PSScriptRoot "WootkitSensor.c"
$obj = Join-Path $objDir "WootkitSensor.obj"
$sys = Join-Path $outDir "WootkitSensor.sys"

$includeArgs = @("/I", $kmInclude, "/I", $sharedIncludePath, "/I", $msvcInclude)
if ($ucrtIncludePath) {
    $includeArgs += @("/I", $ucrtIncludePath)
}

& $cl /nologo /c /kernel /W4 /WX- /O2 /GS /Zl /TC `
    /D_AMD64_ /DAMD64 /DNTDDI_VERSION=NTDDI_WIN10 /D_WIN32_WINNT=0x0A00 `
    @includeArgs /Fo$obj $src
if ($LASTEXITCODE -ne 0) {
    throw "cl.exe failed with exit code $LASTEXITCODE"
}

& $link /nologo /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry /MACHINE:X64 /NODEFAULTLIB /INTEGRITYCHECK `
    /LIBPATH:$kmLib /OUT:$sys $obj ntoskrnl.lib BufferOverflowK.lib
if ($LASTEXITCODE -ne 0) {
    throw "link.exe failed with exit code $LASTEXITCODE"
}

Write-Host "Built $sys"
