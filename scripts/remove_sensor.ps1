$ErrorActionPreference = "Continue"

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Admin)) {
    throw "Run this script from an Administrator PowerShell."
}

$driverPath = Join-Path $env:SystemRoot "System32\drivers\WootkitSensor.sys"

sc.exe stop WootkitSensor | Out-Host
for ($i = 0; $i -lt 20; $i++) {
    $service = Get-Service -Name "WootkitSensor" -ErrorAction SilentlyContinue
    if (-not $service -or $service.Status -eq "Stopped") {
        break
    }

    Start-Sleep -Milliseconds 500
}

sc.exe delete WootkitSensor | Out-Host

if (Test-Path $driverPath) {
    try {
        Remove-Item -LiteralPath $driverPath -Force -ErrorAction Stop
        Write-Host "Removed $driverPath"
    }
    catch {
        Write-Warning "Could not remove $driverPath because Windows still has it open. Reboot, then run this script again."
    }
}
else {
    Write-Host "Driver file is already removed."
}
