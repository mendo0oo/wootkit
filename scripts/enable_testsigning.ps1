$ErrorActionPreference = "Stop"

Write-Host "Enabling Windows test signing. Reboot required."
bcdedit /set testsigning on
