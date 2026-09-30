[CmdletBinding()]
param(
    [string]$Target = "rpi@10.123.71.141"
)

$ErrorActionPreference = "Stop"
$installer = Join-Path $PSScriptRoot "install-rpi.sh"

if (-not (Test-Path -LiteralPath $installer)) {
    throw "Installer not found: $installer"
}

Write-Host "[1/2] Uploading the Raspberry Pi installer..."
& scp $installer "${Target}:/tmp/install-rpi.sh"
if ($LASTEXITCODE -ne 0) { throw "Installer upload failed" }

Write-Host "[2/2] Installing dependencies, RAM disk and scaner.service..."
& ssh -t $Target "sudo bash /tmp/install-rpi.sh"
if ($LASTEXITCODE -ne 0) { throw "Raspberry Pi bootstrap failed" }

Write-Host "Bootstrap completed. Run .\deploy.ps1 to build and deploy."

