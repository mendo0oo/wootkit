$ErrorActionPreference = "Stop"

$src = Join-Path $PSScriptRoot "WootkitCollector.cpp"
$out = Join-Path $PSScriptRoot "WootkitCollector.exe"
g++ -std=c++20 -O2 -Wall -Wextra -municode -DUNICODE -D_UNICODE $src -o $out
Write-Host "Built $out"
