$ErrorActionPreference = "Continue"

sc.exe stop WootkitSensor
sc.exe delete WootkitSensor
Remove-Item -LiteralPath "$env:SystemRoot\System32\drivers\WootkitSensor.sys" -Force -ErrorAction SilentlyContinue
