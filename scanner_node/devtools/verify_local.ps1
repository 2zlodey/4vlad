[CmdletBinding()]
param(
    [switch]$SkipPython,
    [switch]$RunE2E,
    [ValidateRange(0, 255)]
    [int]$FrontendId = 0
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$workspaceRoot = (Resolve-Path (Join-Path $projectRoot '..\..')).Path
$buildDir = Join-Path $projectRoot 'build'

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

if ($RunE2E) {
    if (-not $python) {
        $python = Join-Path $workspaceRoot '.venv\Scripts\python.exe'
        if (-not (Test-Path $python)) {
            $python = (Get-Command python -ErrorAction Stop).Source
        }
    }
    Invoke-Checked $python @((Join-Path $projectRoot 'tools\radio_e2e_test.py'),
        '--client', (Join-Path $buildDir 'scanner_node.exe'),
        '--device-json', (Join-Path $projectRoot '..\device.json'),
        '--frontend-id', "$FrontendId")
}

Write-Host 'Local verification passed.'
