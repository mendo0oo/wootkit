param(
    [switch]$Install,
    [switch]$EnableTestSigning,
    [switch]$CreateSigningCert,
    [switch]$Build,
    [switch]$SignDriver,
    [switch]$PrepareHost,
    [switch]$Full
)

$ErrorActionPreference = "Stop"

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-CommandExists {
    param([Parameter(Mandatory)][string]$Name)
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Get-VisualStudioPath {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        return $null
    }

    $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $path) {
        return $null
    }

    return $path
}

function Test-Msvc {
    $vsPath = Get-VisualStudioPath
    if (-not $vsPath) {
        return $false
    }

    $msvcRoot = Join-Path $vsPath "VC\Tools\MSVC"
    if (-not (Test-Path $msvcRoot)) {
        return $false
    }

    $msvc = Get-ChildItem -Directory $msvcRoot | Sort-Object Name -Descending | Select-Object -First 1
    if (-not $msvc) {
        return $false
    }

    return (Test-Path (Join-Path $msvc.FullName "bin\HostX86\x64\cl.exe"))
}

function Test-Wdk {
    $kitsInclude = "${env:ProgramFiles(x86)}\Windows Kits\10\Include"
    if (-not (Test-Path $kitsInclude)) {
        return $false
    }

    $kit = Get-ChildItem -Directory $kitsInclude |
        Where-Object { Test-Path (Join-Path $_.FullName "km\ntifs.h") } |
        Sort-Object Name -Descending |
        Select-Object -First 1

    return $null -ne $kit
}

function Install-WingetPackage {
    param(
        [Parameter(Mandatory)][string]$Id,
        [string]$Override
    )

    if (-not (Test-CommandExists winget)) {
        throw "winget was not found. Install App Installer from Microsoft Store, then rerun this script."
    }

    $args = @(
        "install",
        "--id", $Id,
        "--exact",
        "--source", "winget",
        "--accept-package-agreements",
        "--accept-source-agreements"
    )

    if ($Override) {
        $args += @("--override", $Override)
    }

    & winget @args
    if ($LASTEXITCODE -ne 0) {
        throw "winget install failed for $Id with exit code $LASTEXITCODE"
    }
}

Write-Host "Wootkit requirement bootstrap"
Write-Host ""

if ($PrepareHost -or $Full) {
    $Install = $true
    $EnableTestSigning = $true
    $CreateSigningCert = $true
}

if ($Full) {
    $Build = $true
    $SignDriver = $true
}

$checks = [ordered]@{
    "Administrator shell" = Test-Admin
    "winget" = Test-CommandExists winget
    "Git" = Test-CommandExists git
    "MSVC x64 tools" = Test-Msvc
    "Windows Driver Kit" = Test-Wdk
    "Signing tool" = Test-Path "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
}

foreach ($item in $checks.GetEnumerator()) {
    $status = if ($item.Value) { "OK" } else { "MISSING" }
    Write-Host ("{0,-24} {1}" -f $item.Key, $status)
}

if ($Install) {
    Write-Host ""
    Write-Host "Installing missing requirements..."

    if (-not (Test-Admin)) {
        throw "Run this script from an Administrator PowerShell when using -Install."
    }

    if (-not (Test-CommandExists git)) {
        Install-WingetPackage -Id "Git.Git"
    }

    if (-not (Test-Msvc)) {
        Install-WingetPackage `
            -Id "Microsoft.VisualStudio.2022.BuildTools" `
            -Override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    }

    if (-not (Test-Wdk)) {
        Install-WingetPackage -Id "Microsoft.WindowsWDK.10"
    }

    Write-Host ""
    Write-Host "Install pass complete. Reopen PowerShell if new tools were installed."
}

if ($EnableTestSigning) {
    if (-not (Test-Admin)) {
        throw "Run this script from an Administrator PowerShell when using -EnableTestSigning."
    }

    & (Join-Path $PSScriptRoot "enable_testsigning.ps1")
    Write-Host "Restart required before loading the driver."
}

if ($Build) {
    & (Join-Path $PSScriptRoot "..\driver\build_driver.ps1")
    & (Join-Path $PSScriptRoot "..\collector\build_collector.ps1")
}

if ($CreateSigningCert) {
    if (-not (Test-Admin)) {
        throw "Run this script from an Administrator PowerShell when using -CreateSigningCert."
    }

    $driverPath = Join-Path $PSScriptRoot "..\driver\x64\Release\WootkitSensor.sys"
    if (-not (Test-Path $driverPath)) {
        Write-Host "Driver is not built yet; creating the signing certificate after a temporary build."
        & (Join-Path $PSScriptRoot "..\driver\build_driver.ps1")
    }

    & (Join-Path $PSScriptRoot "sign_driver.ps1")
}

if ($SignDriver -and -not $CreateSigningCert) {
    & (Join-Path $PSScriptRoot "sign_driver.ps1")
}

Write-Host ""
Write-Host "Next steps:"
Write-Host "  .\scripts\bootstrap_requirements.ps1 -PrepareHost"
Write-Host "  Restart-Computer"
Write-Host "  .\scripts\bootstrap_requirements.ps1 -Build -SignDriver"
Write-Host "  .\scripts\install_sensor.ps1"
