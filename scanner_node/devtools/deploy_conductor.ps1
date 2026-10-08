[CmdletBinding()]
param(
    [string]$SshTarget = 'vlad@np.lora-wan.net',
    [ValidateRange(1, 65535)][int]$Port = 2222,
    [string]$ExpectedHostName = 'conductor',
    [string]$RemoteRoot = '/home/vlad/scanner-deploy',
    [string]$IdentityFile,
    [switch]$BuildOnly,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
if ($SshTarget -notmatch '^[A-Za-z0-9_.-]+@[A-Za-z0-9.-]+$') {
    throw 'SshTarget must be user@host (IPv4 or hostname).'
}
if ($ExpectedHostName -notmatch '^[A-Za-z0-9_.-]+$') { throw 'Invalid expected hostname.' }
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
    'scanner_node/include', 'scanner_node/src',
    'scanner_node/perf_lib/perf_probe.c', 'scanner_node/perf_lib/perf_probe.h',
    'scanner_node/perf_lib/perf_probe_hal.h', 'scanner_node/perf_lib/perf_probe_hal_host.c',
    'scanner_node/devtools/build_remote_stub.sh',
    'scanner_node/tests/log_tests.c', 'scanner_node/tests/radio_reinitialize_tests.c',
    'scanner_node/tests/commutator_tests.c',
    'scanner_node/tools/demo_server_mock.py', 'scanner_node/tools/radio_e2e_test.py',
    'scanner_node/tools/logging_e2e_test.py'
)
foreach ($entry in $manifest) {
    if (-not (Test-Path -LiteralPath (Join-Path $familyRoot $entry))) { throw "Missing deployment input: $entry" }
}
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$runName = "run_${stamp}_$PID"
$remoteRun = "$($RemoteRoot.TrimEnd('/'))/$runName"
$archiveName = "scanner_${stamp}_$PID.tar.gz"
$buildArgument = if ($BuildOnly) { ' --build-only' } else { '' }
$prepare = "set -eu; test `"`$(hostname)`" = '$ExpectedHostName'; mkdir -p '$RemoteRoot'; mkdir '$remoteRun'"
$build = "set -eu; test `"`$(hostname)`" = '$ExpectedHostName'; tar -xzf '$remoteRun/$archiveName' -C '$remoteRun'; sh '$remoteRun/scanner_node/devtools/build_remote_stub.sh'$buildArgument"

Write-Host "Target: ${SshTarget}:$Port; expected hostname=$ExpectedHostName"
Write-Host "Working-tree source: $familyRoot"
Write-Host "Deployment: $remoteRun"
if ($DryRun) {
    Write-Host 'Dry run: no network, remote writes or build.'
    $manifest | ForEach-Object { Write-Host "  $_" }
    Write-Host "Remote prepare: $prepare"
    Write-Host "Remote build: $build"
    return
}

$ssh = (Get-Command ssh -ErrorAction Stop).Source
$scp = (Get-Command scp -ErrorAction Stop).Source
$tar = (Get-Command tar -ErrorAction Stop).Source
$options = @('-o', 'ConnectTimeout=10', '-o', 'ServerAliveInterval=5', '-o', 'ServerAliveCountMax=2')
if ($IdentityFile) {
    $options += @('-i', $IdentityFile, '-o', 'IdentitiesOnly=yes', '-o', 'BatchMode=yes')
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
$archive = Join-Path $temporary $archiveName
try {
    Invoke-Checked $tar (@('-czf', $archive, '-C', $familyRoot) + $manifest)
    Invoke-Checked $ssh (@('-p', "$Port") + $options + @($SshTarget, $prepare))
    Invoke-Checked $scp (@('-P', "$Port") + $options + @($archive, "${SshTarget}:$remoteRun/$archiveName"))
    Invoke-Checked $ssh (@('-p', "$Port") + $options + @($SshTarget, $build))
    Write-Host "Deployment passed: $remoteRun/scanner_node/build-remote/scanner_node"
    Write-Host 'Scanner is not left running; no service, production server or previous deployment was changed.'
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}