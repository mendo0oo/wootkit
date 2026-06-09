param(
    [switch]$NoAutoSign
)

$ErrorActionPreference = "Stop"

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-TestSigningEnabled {
    $output = & bcdedit /enum "{current}" 2>$null
    if ($LASTEXITCODE -ne 0) {
        return $false
    }

    return ($output -match "testsigning\s+Yes")
}

function Test-ValidSignature {
    param([Parameter(Mandatory)][string]$Path)

    $signature = Get-AuthenticodeSignature -FilePath $Path
    return $signature.Status -eq "Valid"
}

function Invoke-Sc {
    param([Parameter(Mandatory)][string[]]$Arguments)

    & sc.exe @Arguments
    return $LASTEXITCODE
}

function Stop-WootkitService {
    $queryExit = Invoke-Sc -Arguments @("query", "WootkitSensor")
    if ($queryExit -ne 0) {
        return
    }

    Write-Host "Stopping existing WootkitSensor service"
    Invoke-Sc -Arguments @("stop", "WootkitSensor") | Out-Null

    for ($i = 0; $i -lt 20; $i++) {
        $service = Get-Service -Name "WootkitSensor" -ErrorAction SilentlyContinue
        if (-not $service -or $service.Status -eq "Stopped") {
            return
        }

        Start-Sleep -Milliseconds 500
    }
}

if (-not (Test-Admin)) {
    throw "Run this script from an Administrator PowerShell."
}

$driverDir = Resolve-Path (Join-Path $PSScriptRoot "..\driver")
$sys = Join-Path $driverDir "x64\Release\WootkitSensor.sys"
$target = Join-Path $env:SystemRoot "System32\drivers\WootkitSensor.sys"
$signScript = Join-Path $PSScriptRoot "sign_driver.ps1"

if (-not (Test-Path $sys)) {
    throw "Build the driver first. Expected: $sys"
}

if (-not (Test-TestSigningEnabled)) {
    throw "Windows test-signing is not enabled for this boot entry. Run .\scripts\bootstrap_requirements.ps1 -PrepareHost, reboot, then run install again."
}

if (-not (Test-ValidSignature -Path $sys)) {
    if ($NoAutoSign) {
        throw "Driver is not signed. Run .\scripts\bootstrap_requirements.ps1 -Build -SignDriver first."
    }

    Write-Host "Driver is not signed with a trusted certificate. Signing now..."
    & $signScript -DriverPath $sys
}

if (-not (Test-ValidSignature -Path $sys)) {
    throw "Driver signature is still not valid after signing. Check the local certificate trust store."
}

Stop-WootkitService

Write-Host "Copying driver to $target"
try {
    Copy-Item -LiteralPath $sys -Destination $target -Force
}
catch {
    throw "Could not replace $target because Windows still has it open. Run .\scripts\remove_sensor.ps1, reboot, then run .\scripts\install_sensor.ps1 again. Original error: $($_.Exception.Message)"
}

Write-Host "Creating or updating WootkitSensor service"
$queryExit = Invoke-Sc -Arguments @("query", "WootkitSensor")
if ($queryExit -eq 0) {
    Invoke-Sc -Arguments @("config", "WootkitSensor", "type=", "kernel", "start=", "system", "binPath=", "System32\drivers\WootkitSensor.sys", "DisplayName=", "Wootkit Defensive Kernel Sensor") | Out-Null
} else {
    Invoke-Sc -Arguments @("create", "WootkitSensor", "type=", "kernel", "start=", "system", "binPath=", "System32\drivers\WootkitSensor.sys", "DisplayName=", "Wootkit Defensive Kernel Sensor") | Out-Null
}

Write-Host "Starting WootkitSensor"
$startExit = Invoke-Sc -Arguments @("start", "WootkitSensor")
if ($startExit -ne 0) {
    throw "Failed to start WootkitSensor. If this is error 577, rerun .\scripts\bootstrap_requirements.ps1 -PrepareHost, reboot, then .\scripts\bootstrap_requirements.ps1 -Build -SignDriver."
}

Write-Host "WootkitSensor installed and started."
