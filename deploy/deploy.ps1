[CmdletBinding()]
param(
    [string]$Target = "rpi@10.123.71.141",
    [int]$Port = 2200,
    [switch]$StartService
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$remoteSource = "/mnt/scaner-ram/build/src"

function Invoke-Ssh {
    param([Parameter(Mandatory)][string]$Command)
    & ssh $Target $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Remote command failed with exit code $LASTEXITCODE"
    }
}

Write-Host "[1/5] Preparing the RAM build directory..."
Invoke-Ssh "mkdir -p '$remoteSource/cJSON' && rm -f '$remoteSource/device.bin'"

Write-Host "[2/5] Uploading the minimal source set..."
$sourceFiles = @(
    (Join-Path $projectRoot "main.c")
    (Join-Path $projectRoot "device.c")
    (Join-Path $projectRoot "device.h")
    (Join-Path $projectRoot "device.json")
    "${Target}:${remoteSource}/"
)
& scp @sourceFiles
if ($LASTEXITCODE -ne 0) { throw "Source upload failed" }

$cJsonFiles = @(
    (Join-Path $projectRoot "cJSON/cJSON.c")
    (Join-Path $projectRoot "cJSON/cJSON.h")
    "${Target}:${remoteSource}/cJSON/"
)
& scp @cJsonFiles
if ($LASTEXITCODE -ne 0) { throw "cJSON upload failed" }

Write-Host "[3/5] Building on Raspberry Pi in RAM..."
Invoke-Ssh "cd '$remoteSource' && gcc -Wall -Wextra -std=c11 main.c device.c cJSON/cJSON.c -I./cJSON -o device.bin"

Write-Host "[4/5] Installing the accepted artifact..."
Invoke-Ssh "sudo install -m 0755 '$remoteSource/device.bin' /opt/scaner/device.bin && sudo install -m 0644 '$remoteSource/device.json' /etc/scaner/device.json && sudo systemctl enable scaner.service"

Write-Host "[5/5] Verifying the installed binary..."
Invoke-Ssh "file /opt/scaner/device.bin && getconf LONG_BIT"

if ($StartService) {
    Write-Host "Starting scaner.service..."
    Invoke-Ssh "sudo systemctl restart scaner.service; sudo systemctl --no-pager --full status scaner.service"
} else {
    Write-Warning "The service was installed but not started. Use -StartService after reviewing the 32/64-bit protocol compatibility warning."
}

