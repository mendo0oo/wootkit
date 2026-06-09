$ErrorActionPreference = "Stop"

$driverDir = Resolve-Path (Join-Path $PSScriptRoot "..\driver")
$sys = Join-Path $driverDir "x64\Release\WootkitSensor.sys"
$inf = Join-Path $driverDir "WootkitSensor.inf"

if (-not (Test-Path $sys)) {
    throw "Build the driver first. Expected: $sys"
}

Copy-Item -LiteralPath $sys -Destination "$env:SystemRoot\System32\drivers\WootkitSensor.sys" -Force
pnputil /add-driver $inf /install
sc.exe create WootkitSensor type= kernel start= system binPath= System32\drivers\WootkitSensor.sys DisplayName= "Wootkit Defensive Kernel Sensor" 2>$null
sc.exe start WootkitSensor
