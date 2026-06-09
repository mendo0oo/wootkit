$ErrorActionPreference = "Continue"

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Admin)) {
    throw "Run this script from an Administrator PowerShell."
}

$driversDir = Join-Path $env:SystemRoot "System32\drivers"

sc.exe stop WootkitSensor | Out-Host
for ($i = 0; $i -lt 20; $i++) {
    $service = Get-Service -Name "WootkitSensor" -ErrorAction SilentlyContinue
    if (-not $service -or $service.Status -eq "Stopped") {
        break
    }

    Start-Sleep -Milliseconds 500
}

sc.exe delete WootkitSensor | Out-Host

$driverFiles = Get-ChildItem -LiteralPath $driversDir -Filter "WootkitSensor*.sys" -ErrorAction SilentlyContinue
if ($driverFiles) {
    $locked = $false
    foreach ($driverFile in $driverFiles) {
        $driverPath = $driverFile.FullName
        try {
            Remove-Item -LiteralPath $driverPath -Force -ErrorAction Stop
            Write-Host "Removed $driverPath"
        }
        catch {
            $locked = $true
            Write-Warning "Could not remove $driverPath because Windows still has it open."
        }
    }

    if ($locked) {
        Write-Warning "Reboot, then run this script again to remove locked driver files."
    }
}
else {
    Write-Host "Driver files are already removed."
}
