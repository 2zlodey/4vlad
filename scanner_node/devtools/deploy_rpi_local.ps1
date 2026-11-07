[CmdletBinding()]
param(
    [string]$SshTarget = 'rpi@10.76.195.141',
    [ValidateRange(1, 65535)][int]$Port = 22,
    [string]$ExpectedHostName = '',
    [string]$RemoteRoot = '/home/rpi/scanner-deploy',
    [string]$IdentityFile,
    [switch]$UseExistingToolchain,
    [switch]$HardwareBuild,
    [switch]$StartScanner,
    [string]$ServerAddress = '82.165.20.164',
    [ValidateRange(1, 65535)][int]$ServerPort = 2653,
    [ValidateRange(1, 65535)][int]$LocalPort = 3333,
    [ValidateRange(1, 65535)][int]$ReplyPort = 3333,
    [switch]$BuildOnly,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
if ($SshTarget -notmatch '^[A-Za-z0-9_.-]+@[A-Za-z0-9.-]+$') {
    throw 'SshTarget must be user@host (IPv4 or hostname).'
}
if ($UseExistingToolchain -and $HardwareBuild) { throw 'Choose either -UseExistingToolchain or -HardwareBuild.' }
if ($ServerAddress -notmatch '^[0-9]{1,3}(\.[0-9]{1,3}){3}$') { throw 'ServerAddress must be an IPv4 address.' }
if ($ExpectedHostName -and $ExpectedHostName -notmatch '^[A-Za-z0-9_.-]+$') { throw 'Invalid expected hostname.' }
if ($RemoteRoot -notmatch '^/[A-Za-z0-9_./-]+$' -or $RemoteRoot -match '(^|/)\.\.(/|$)' -or $RemoteRoot -eq '/') {
    throw 'RemoteRoot must be an absolute, non-root POSIX path without spaces or parent traversal.'
}
if ($IdentityFile -and -not (Test-Path -LiteralPath $IdentityFile -PathType Leaf)) {
    throw "SSH key not found: $IdentityFile"
}
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$familyRoot = Split-Path -Parent $projectRoot
$manifest = @(
    'cJSON/cJSON.c', 'cJSON/cJSON.h', 'device.json',
    'scanner_node/CMakeLists.txt',
    'scanner_node/include', 'scanner_node/src',
    'scanner_node/perf_lib/perf_probe.c', 'scanner_node/perf_lib/perf_probe.h',
    'scanner_node/perf_lib/perf_probe_hal.h', 'scanner_node/perf_lib/perf_probe_hal_host.c',
    'scanner_node/devtools/build_remote_stub.sh', 'scanner_node/devtools/setup_conductor.sh',
    'scanner_node/tests/log_tests.c', 'scanner_node/tests/radio_reinitialize_tests.c',
    'scanner_node/tests/commutator_tests.c',
    'scanner_node/tools/bladerf_band_capture.c', 'scanner_node/tools/sdr_sweep_benchmark.c',
    'scanner_node/tools/demo_server_mock.py', 'scanner_node/tools/radio_e2e_test.py',
    'scanner_node/tools/logging_e2e_test.py'
)
foreach ($entry in $manifest) {
    if (-not (Test-Path -LiteralPath (Join-Path $familyRoot $entry))) { throw "Missing deployment input: $entry" }
}
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$runName = "run_${stamp}_$PID"
$remoteRun = "$($RemoteRoot.TrimEnd('/'))/$runName"
$remoteNode = "$remoteRun/scanner_node"
$remoteBuildDir = "$remoteNode/build-hardware"
$remoteBinary = if ($HardwareBuild) { "$remoteBuildDir/scanner_node" } else { "$remoteNode/build-remote/scanner_node" }
$archiveName = "scanner_${stamp}_$PID.tar.gz"
$buildArgument = if ($BuildOnly) { ' --build-only' } else { '' }
$hostCheck = if ($ExpectedHostName) { "test `"`$(hostname)`" = '$ExpectedHostName'; " } else { '' }
$prepare = "set -eu; $hostCheck mkdir -p '$RemoteRoot'; mkdir '$remoteRun'"
$buildCommand = if ($UseExistingToolchain) {
    "sh '$remoteRun/scanner_node/devtools/build_remote_stub.sh'$buildArgument"
} elseif ($HardwareBuild) {
    "cmake -S '$remoteNode' -B '$remoteBuildDir' -G Ninja -DCMAKE_BUILD_TYPE=Release -DSCANNER_ENABLE_STUB_SDR=OFF -DSCANNER_ENABLE_PERF=ON -DBUILD_TESTING=OFF; cmake --build '$remoteBuildDir' --parallel --target scanner_node"
} else {
    "bash '$remoteRun/scanner_node/devtools/setup_conductor.sh'$buildArgument"
}
$sudoCheck = if ($UseExistingToolchain -or $HardwareBuild) { '' } else { 'sudo -v; ' }
$build = "set -eu; $hostCheck ${sudoCheck}tar -xzf '$remoteRun/$archiveName' -C '$remoteRun'; $buildCommand"
$run = "set -eu; $hostCheck cd '$remoteNode'; exec '$remoteBinary' --server '$ServerAddress' --server-port $ServerPort --local-port $LocalPort --reply-port $ReplyPort --device-json ../device.json"

Write-Host "Target: ${SshTarget}:$Port"
if ($ExpectedHostName) { Write-Host "Expected hostname: $ExpectedHostName" } else { Write-Host 'Hostname check: disabled; pass -ExpectedHostName to enable it.' }
if ($IdentityFile) { Write-Host "SSH identity: $IdentityFile" }
if ($UseExistingToolchain) { Write-Host 'Build: existing toolchain; no sudo or package installation.' }
if ($HardwareBuild) { Write-Host 'Build: BladeRF/HackRF enabled, Stub SDR disabled; no package installation.' }
Write-Host "Working-tree source: $familyRoot"
Write-Host "Deployment: $remoteRun"
if ($StartScanner) { Write-Host "Run: local port $LocalPort, reply port $ReplyPort, server ${ServerAddress}:$ServerPort" }
if ($DryRun) {
    Write-Host 'Dry run: no network, remote writes or build.'
    $manifest | ForEach-Object { Write-Host "  $_" }
    Write-Host "Remote prepare: $prepare"
    Write-Host "Remote build: $build"
    if ($StartScanner) { Write-Host "Remote run: $run" }
    return
}

$ssh = (Get-Command ssh -ErrorAction Stop).Source
$scp = (Get-Command scp -ErrorAction Stop).Source
$tar = (Get-Command tar -ErrorAction Stop).Source
$options = @('-o', 'ConnectTimeout=10', '-o', 'ServerAliveInterval=5', '-o', 'ServerAliveCountMax=2')
if ($IdentityFile) {
    $options += @('-i', $IdentityFile, '-o', 'IdentitiesOnly=yes')
} else {
    $options += @('-o', 'PreferredAuthentications=password', '-o', 'PubkeyAuthentication=no',
                  '-o', 'NumberOfPasswordPrompts=1')
}

function Invoke-Checked {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE" }
}

$temporary = Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($temporary)
$stageRoot = Join-Path $temporary 'payload'
[void][IO.Directory]::CreateDirectory($stageRoot)
$archive = Join-Path $temporary $archiveName
try {
    foreach ($entry in $manifest) {
        $sourcePath = Join-Path $familyRoot $entry
        $stagePath = Join-Path $stageRoot $entry
        [void][IO.Directory]::CreateDirectory((Split-Path -Parent $stagePath))
        Copy-Item -LiteralPath $sourcePath -Destination $stagePath -Recurse -Force
    }
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    Get-ChildItem -LiteralPath $stageRoot -Filter '*.sh' -File -Recurse | ForEach-Object {
        $contents = [IO.File]::ReadAllText($_.FullName)
        $contents = $contents.Replace("`r`n", "`n").Replace("`r", "`n")
        [IO.File]::WriteAllText($_.FullName, $contents, $utf8NoBom)
    }
    Invoke-Checked $tar (@('-czf', $archive, '-C', $stageRoot) + $manifest)
    Invoke-Checked $ssh (@('-p', "$Port") + $options + @($SshTarget, $prepare))
    Invoke-Checked $scp (@('-P', "$Port") + $options + @($archive, "${SshTarget}:$remoteRun/$archiveName"))
    $buildSshOptions = if ($UseExistingToolchain -or $HardwareBuild) { @('-p', "$Port") + $options } else { @('-tt', '-p', "$Port") + $options }
    Invoke-Checked $ssh ($buildSshOptions + @($SshTarget, $build))
    Write-Host "Deployment passed: $remoteBinary"
    if ($StartScanner) {
        Write-Host 'Starting scanner in the foreground. Press Ctrl+C to stop it.'
        Invoke-Checked $ssh (@('-tt', '-p', "$Port") + $options + @($SshTarget, $run))
    } else {
        Write-Host 'Scanner was not started; no service or previous deployment was changed.'
    }
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}