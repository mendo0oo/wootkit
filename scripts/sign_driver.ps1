param(
    [string]$DriverPath = (Join-Path $PSScriptRoot "..\driver\x64\Release\WootkitSensor.sys"),
    [string]$CertificateName = "Wootkit Kernel Sensor Test Signing"
)

$ErrorActionPreference = "Stop"

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-LatestWindowsKitVersion {
    $kitsBin = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
    if (-not (Test-Path $kitsBin)) {
        return $null
    }

    Get-ChildItem -Directory $kitsBin |
        Where-Object { Test-Path (Join-Path $_.FullName "x64\signtool.exe") } |
        Sort-Object Name -Descending |
        Select-Object -First 1
}

if (-not (Test-Admin)) {
    throw "Run this script from an Administrator PowerShell."
}

$resolvedDriver = Resolve-Path $DriverPath -ErrorAction SilentlyContinue
if (-not $resolvedDriver) {
    throw "Driver was not found. Build it first: .\driver\build_driver.ps1"
}

$kit = Get-LatestWindowsKitVersion
if (-not $kit) {
    throw "signtool.exe was not found. Install the Windows Driver Kit."
}

$signtool = Join-Path $kit.FullName "x64\signtool.exe"
$cert = Get-ChildItem Cert:\LocalMachine\My |
    Where-Object { $_.Subject -eq "CN=$CertificateName" } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if (-not $cert) {
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject "CN=$CertificateName" `
        -CertStoreLocation "Cert:\LocalMachine\My" `
        -KeyAlgorithm RSA `
        -KeyLength 2048 `
        -HashAlgorithm SHA256 `
        -NotAfter (Get-Date).AddYears(5)
}

$rootStore = [System.Security.Cryptography.X509Certificates.X509Store]::new("Root", "LocalMachine")
$publisherStore = [System.Security.Cryptography.X509Certificates.X509Store]::new("TrustedPublisher", "LocalMachine")
$rootStore.Open("ReadWrite")
$publisherStore.Open("ReadWrite")
try {
    if (-not ($rootStore.Certificates | Where-Object { $_.Thumbprint -eq $cert.Thumbprint })) {
        $rootStore.Add($cert)
    }

    if (-not ($publisherStore.Certificates | Where-Object { $_.Thumbprint -eq $cert.Thumbprint })) {
        $publisherStore.Add($cert)
    }
}
finally {
    $rootStore.Close()
    $publisherStore.Close()
}

& $signtool sign /v /fd SHA256 /sm /s My /n $CertificateName $resolvedDriver.Path
if ($LASTEXITCODE -ne 0) {
    throw "signtool.exe failed with exit code $LASTEXITCODE"
}

Write-Host "Signed $($resolvedDriver.Path)"
Write-Host "Certificate: $CertificateName"
Write-Host "Thumbprint: $($cert.Thumbprint)"
