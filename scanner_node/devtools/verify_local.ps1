[CmdletBinding()]
param(
    [switch]$SkipPython,
    [switch]$RunE2E,
    [switch]$RunLoggingE2E,
    [ValidateRange(0, 255)]
    [int]$FrontendId = 0
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$workspaceRoot = (Resolve-Path (Join-Path $projectRoot '..\..')).Path
$buildDir = Join-Path $projectRoot 'build-fftw'

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

$cmake = (Get-Command cmake -ErrorAction Stop).Source
if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    if ([string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
        throw 'Set VCPKG_ROOT to a vcpkg checkout containing scripts/buildsystems/vcpkg.cmake.'
    }
    $toolchain = Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'
    if (-not (Test-Path $toolchain)) {
        throw "vcpkg CMake toolchain was not found: $toolchain"
    }
    Invoke-Checked $cmake @('-S', $projectRoot, '-B', $buildDir, '-G', 'Ninja',
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        '-DVCPKG_TARGET_TRIPLET=x64-mingw-static',
        '-DVCPKG_HOST_TRIPLET=x64-mingw-static')
}
Invoke-Checked $cmake @('--build', $buildDir, '--parallel')
Invoke-Checked (Join-Path (Split-Path $cmake) 'ctest.exe') @('--test-dir', $buildDir, '--output-on-failure')

if (-not $SkipPython) {
    $python = Join-Path $workspaceRoot '.venv\Scripts\python.exe'
    if (-not (Test-Path $python)) {
        $python = (Get-Command python -ErrorAction Stop).Source
    }
    Invoke-Checked $python @('-m', 'unittest', 'discover', '-s', (Join-Path $projectRoot 'tests'),
        '-p', 'test_demo_server_mock.py', '-v')
}

if ($RunE2E -or $RunLoggingE2E) {
    if (-not $python) {
        $python = Join-Path $workspaceRoot '.venv\Scripts\python.exe'
        if (-not (Test-Path $python)) {
            $python = (Get-Command python -ErrorAction Stop).Source
        }
    }
    if ($RunLoggingE2E) {
        if ($FrontendId -ne 0) { throw '-RunLoggingE2E currently starts with frontend 0; omit -FrontendId.' }
        Invoke-Checked $python @((Join-Path $projectRoot 'tools\logging_e2e_test.py'),
            '--client', (Join-Path $buildDir 'scanner_node.exe'),
            '--device-json', (Join-Path $projectRoot '..\device.json'))
    } else {
        Invoke-Checked $python @((Join-Path $projectRoot 'tools\radio_e2e_test.py'),
            '--client', (Join-Path $buildDir 'scanner_node.exe'),
            '--device-json', (Join-Path $projectRoot '..\device.json'),
            '--frontend-id', "$FrontendId")
    }
}

Write-Host 'Local verification passed.'
