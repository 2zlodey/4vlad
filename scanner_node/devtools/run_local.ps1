[CmdletBinding()]
param(
    [string]$Generator,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [string]$BuildDir = 'build-windows',
    [string]$ServerAddress = '82.165.20.164',
    [ValidateRange(1, 65535)]
    [int]$ServerPort = 2653,
    [ValidateRange(1, 65535)]
    [int]$LocalPort = 3333,
    [ValidateRange(0, 65535)]
    [int]$ReplyPort = 0,
    [switch]$BuildOnly
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$workspaceRoot = (Resolve-Path (Join-Path $projectRoot '..')).Path
$buildPath = if ([IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir
} else {
    Join-Path $projectRoot $BuildDir
}
$deviceJson = Join-Path $workspaceRoot 'device.json'
$cmake = (Get-Command cmake -ErrorAction Stop).Source
$cmakeVersionText = & $cmake --version
$cmakeVersionMatch = [regex]::Match(($cmakeVersionText -join "`n"), 'cmake version\s+(\d+\.\d+(?:\.\d+)?)')
if ($LASTEXITCODE -ne 0 -or -not $cmakeVersionMatch.Success) {
    throw 'Could not determine the installed CMake version.'
}
$cmakeVersion = [version]$cmakeVersionMatch.Groups[1].Value
if ($cmakeVersion -lt [version]'3.16') {
    throw "CMake 3.16 or newer is required; found $cmakeVersion. Update CMake and rerun this script."
}

if (-not (Test-Path $deviceJson)) {
    throw "Device configuration not found: $deviceJson"
}

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

$configureArgs = @(
    '-S', $projectRoot,
    '-B', $buildPath,
    "-DCMAKE_BUILD_TYPE=$Configuration",
    '-DSCANNER_ENABLE_STUB_SDR=ON',
    '-DBUILD_TESTING=OFF'
)
if (-not [string]::IsNullOrWhiteSpace($Generator)) {
    $configureArgs += @('-G', $Generator)
}
Invoke-Checked $cmake $configureArgs
Invoke-Checked $cmake @('--build', $buildPath, '--config', $Configuration, '--parallel')

$executableCandidates = @(
    (Join-Path $buildPath "$Configuration\scanner_node.exe"),
    (Join-Path $buildPath 'scanner_node.exe')
)
$scannerExe = $executableCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $scannerExe) {
    throw "Built scanner_node.exe was not found under: $buildPath"
}
Write-Host "Build complete: $scannerExe"

if ($BuildOnly) {
    return
}

$nodeArgs = @(
    '--server', $ServerAddress,
    '--server-port', "$ServerPort",
    '--local-port', "$LocalPort",
    '--device-json', $deviceJson
)
if ($ReplyPort -gt 0) {
    $nodeArgs += @('--reply-port', "$ReplyPort")
}
$effectiveReplyPort = if ($ReplyPort -gt 0) { $ReplyPort } else { $LocalPort }
Write-Host "Starting scanner node for ${ServerAddress}:$ServerPort (local port $LocalPort, reply port $effectiveReplyPort). Press Ctrl+C to stop it."
& $scannerExe @nodeArgs
if ($LASTEXITCODE -ne 0) {
    throw "scanner_node.exe failed with exit code $LASTEXITCODE"
}