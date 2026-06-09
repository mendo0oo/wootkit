$ErrorActionPreference = "Stop"

$src = Join-Path $PSScriptRoot "WootkitCollector.cpp"
$out = Join-Path $PSScriptRoot "WootkitCollector.exe"

$gpp = Get-Command g++ -ErrorAction SilentlyContinue
if ($gpp) {
    & $gpp.Source -std=c++20 -O2 -Wall -Wextra -municode -DUNICODE -D_UNICODE $src -o $out
    if ($LASTEXITCODE -ne 0) {
        throw "g++ failed with exit code $LASTEXITCODE"
    }
    Write-Host "Built $out"
    exit 0
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "Neither g++ nor Visual Studio Build Tools were found."
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw "Neither g++ nor MSVC C++ tools were found."
}

$msvcRoot = Join-Path $vsPath "VC\Tools\MSVC"
$msvc = Get-ChildItem -Directory $msvcRoot | Sort-Object Name -Descending | Select-Object -First 1
if (-not $msvc) {
    throw "MSVC toolchain was not found under $msvcRoot."
}

$kitsInclude = "${env:ProgramFiles(x86)}\Windows Kits\10\Include"
$kit = Get-ChildItem -Directory $kitsInclude |
    Where-Object { Test-Path (Join-Path $_.FullName "um\windows.h") } |
    Sort-Object Name -Descending |
    Select-Object -First 1
if (-not $kit) {
    throw "Windows SDK headers were not found."
}

$kitVersion = $kit.Name
$kitRoot = Split-Path $kitsInclude -Parent
$cl = Join-Path $msvc.FullName "bin\HostX86\x64\cl.exe"
$msvcInclude = Join-Path $msvc.FullName "include"
$msvcLib = Join-Path $msvc.FullName "lib\x64"
$umInclude = Join-Path $kitsInclude "$kitVersion\um"
$sharedInclude = Join-Path $kitsInclude "$kitVersion\shared"
$ucrtInclude = Join-Path $kitsInclude "$kitVersion\ucrt"
$umLib = Join-Path $kitRoot "Lib\$kitVersion\um\x64"
$ucrtLib = Join-Path $kitRoot "Lib\$kitVersion\ucrt\x64"

& $cl /nologo /EHsc /std:c++20 /O2 /W4 /DUNICODE /D_UNICODE `
    /I $msvcInclude /I $umInclude /I $sharedInclude /I $ucrtInclude `
    /Fe$out $src `
    /link /LIBPATH:$msvcLib /LIBPATH:$umLib /LIBPATH:$ucrtLib kernel32.lib
if ($LASTEXITCODE -ne 0) {
    throw "cl.exe failed with exit code $LASTEXITCODE"
}

Write-Host "Built $out"
