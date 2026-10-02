[CmdletBinding()]
param(
    [string]$PiHost = 'rpi@10.123.71.141',
    [string]$IdentityFile = (Join-Path $HOME '.ssh\rpi_scanner_ed25519'),
    [string]$RemoteRoot = '/mnt/scaner-ram/scanner-refactor',
    [ValidateRange(70, 6000)][uint32]$StartMHz = 1000,
    [ValidateRange(70, 6000)][uint32]$StopMHz = 6000,
    [ValidateRange(1, 6000)][uint32]$StepMHz = 10,
    [switch]$BuildOnly
)

$ErrorActionPreference = 'Stop'
if ($StartMHz -ge $StopMHz -or (($StopMHz - $StartMHz) % $StepMHz) -ne 0) {
    throw 'Require StartMHz < StopMHz and an evenly divisible range.'
}
if (-not (Test-Path $IdentityFile)) {
    throw "SSH identity not found: $IdentityFile"
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$familyRoot = Split-Path -Parent $projectRoot
$cJsonRoot = Join-Path $familyRoot 'cJSON'
$deviceJson = Join-Path $familyRoot 'device.json'
$ssh = (Get-Command ssh -ErrorAction Stop).Source
$scp = (Get-Command scp -ErrorAction Stop).Source
$sshOptions = @('-i', $IdentityFile, '-o', 'IdentitiesOnly=yes', '-o', 'ConnectTimeout=10')
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$remoteRunRoot = "$RemoteRoot/run_$stamp"
$remoteNode = "$remoteRunRoot/scanner_node"
$remoteCJson = "$remoteRunRoot/cJSON"
$remoteTools = "$remoteNode/tools"
$remotePerf = "$remoteNode/perf_lib"
$remoteDeviceJson = "$remoteRunRoot/device.json"
$remoteLog = "/home/rpi/sweep_perf_$stamp.log"
$localLog = Join-Path $projectRoot "measurements\sweep_pi_$stamp.log"

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable failed with exit code $LASTEXITCODE"
    }
}

function ConvertTo-ShellLiteral {
    param([Parameter(Mandatory = $true)][string]$Value)
    return "'" + $Value.Replace("'", "'\''") + "'"
}

$remoteNodeLiteral = ConvertTo-ShellLiteral $remoteNode
$remoteCJsonLiteral = ConvertTo-ShellLiteral $remoteCJson
Invoke-Checked $ssh ($sshOptions + @(
    $PiHost,
    "mkdir -p $remoteNodeLiteral $remoteCJsonLiteral '$remoteTools' '$remotePerf'"
))

$sourceDirectories = @(
    (Join-Path $projectRoot 'include'),
    (Join-Path $projectRoot 'src')
)
Invoke-Checked $scp ($sshOptions + @('-r') + $sourceDirectories + @("${PiHost}:$remoteNode/"))
Invoke-Checked $scp ($sshOptions + @(
    (Join-Path $projectRoot 'CMakeLists.txt'),
    (Join-Path $projectRoot 'build_pi.sh'),
    "${PiHost}:$remoteNode/"
))
Invoke-Checked $scp ($sshOptions + @(
    (Join-Path $projectRoot 'perf_lib\perf_probe.c'),
    (Join-Path $projectRoot 'perf_lib\perf_probe.h'),
    (Join-Path $projectRoot 'perf_lib\perf_probe_hal.h'),
    (Join-Path $projectRoot 'perf_lib\perf_probe_hal_host.c'),
    "${PiHost}:$remotePerf/"
))
Invoke-Checked $scp ($sshOptions + @(
    (Join-Path $projectRoot 'tools\bladerf_band_capture.c'),
    (Join-Path $projectRoot 'tools\sdr_sweep_benchmark.c'),
    (Join-Path $projectRoot 'tools\record_iq_from_frontend.py'),
    (Join-Path $projectRoot 'tools\demo_server_mock.py'),
    "${PiHost}:$remoteTools/"
))
Invoke-Checked $scp ($sshOptions + @(
    (Join-Path $cJsonRoot 'cJSON.c'),
    (Join-Path $cJsonRoot 'cJSON.h'),
    "${PiHost}:$remoteCJson/"
))
Invoke-Checked $scp ($sshOptions + @($deviceJson, "${PiHost}:$remoteDeviceJson"))

Invoke-Checked $ssh ($sshOptions + @('-tt', $PiHost, "cd $remoteNodeLiteral && sh ./build_pi.sh"))
if ($BuildOnly) {
    Write-Host "Pi build passed; benchmark skipped. Staged project: $remoteNode"
    return
}

$usbOutput = & $ssh @sshOptions $PiHost 'lsusb'
if ($LASTEXITCODE -ne 0) {
    throw "Could not query USB devices on $PiHost"
}
$usbText = $usbOutput -join "`n"
Write-Host $usbText
if ($usbText -notmatch '2cf0:5250' -or $usbText -notmatch '1d50:6089') {
    throw 'Both BladeRF (2cf0:5250) and HackRF (1d50:6089) must be present before the sweep.'
}

$remoteLogLiteral = ConvertTo-ShellLiteral $remoteLog
$remoteCommand = "cd $remoteNodeLiteral && ./build-pi/scanner_sweep_benchmark $StartMHz $StopMHz $StepMHz > $remoteLogLiteral 2>&1; result=`$?; cat $remoteLogLiteral; exit `$result"
& $ssh @sshOptions $PiHost $remoteCommand
$benchmarkExit = $LASTEXITCODE

$measurements = Split-Path -Parent $localLog
New-Item -ItemType Directory -Force -Path $measurements | Out-Null
& $scp @sshOptions "${PiHost}:$remoteLog" $localLog
$copyExit = $LASTEXITCODE
if ($copyExit -ne 0) {
    throw "Could not copy Pi log; it remains at $remoteLog"
}
if ($benchmarkExit -ne 0) {
    throw "Benchmark failed with exit code $benchmarkExit; log saved to $localLog"
}
Write-Host "Benchmark log saved: $localLog"
