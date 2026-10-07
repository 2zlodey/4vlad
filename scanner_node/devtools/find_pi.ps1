[CmdletBinding()]
param(
    [string[]]$Subnet,
    [string]$UserName = 'rpi',
    [string]$IdentityFile = (Join-Path $HOME '.ssh\rpi_scanner_ed25519'),
    [ValidateRange(100, 10000)][int]$TimeoutMs = 800,
    [ValidateRange(1, 256)][int]$Parallelism = 64,
    [ValidateRange(1, 65536)][int]$MaxHosts = 4096
)

$ErrorActionPreference = 'Stop'
$ssh = (Get-Command ssh -ErrorAction Stop).Source
if (-not (Test-Path -LiteralPath $IdentityFile -PathType Leaf)) {
    throw "SSH identity not found: $IdentityFile"
}

function ConvertTo-IPv4Number {
    param([string]$Address)
    $parsed = [System.Net.IPAddress]::Parse($Address)
    if ($parsed.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
        throw "Not an IPv4 address: $Address"
    }
    $bytes = $parsed.GetAddressBytes()
    return [long]$bytes[0] * 16777216 + [long]$bytes[1] * 65536 + [long]$bytes[2] * 256 + $bytes[3]
}

function ConvertFrom-IPv4Number {
    param([long]$Value)
    return '{0}.{1}.{2}.{3}' -f (($Value -shr 24) -band 255), (($Value -shr 16) -band 255),
        (($Value -shr 8) -band 255), ($Value -band 255)
}

$ranges = @()
if ($Subnet) {
    foreach ($cidr in $Subnet) {
        $parts = $cidr.Split('/')
        if ($parts.Count -ne 2 -or $parts[1] -notmatch '^\d+$' -or [int]$parts[1] -gt 32) {
            throw "Expected IPv4 CIDR, for example 192.168.1.0/24: $cidr"
        }
        $size = [long][Math]::Pow(2, 32 - [int]$parts[1])
        $address = ConvertTo-IPv4Number $parts[0]
        $network = $address - ($address % $size)
        $ranges += [pscustomobject]@{ Network = $network; Size = $size; Label = $cidr }
    }
} else {
    foreach ($adapter in [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces()) {
        if ($adapter.OperationalStatus -ne 'Up' -or $adapter.NetworkInterfaceType -eq 'Loopback') {
            continue
        }
        foreach ($entry in $adapter.GetIPProperties().UnicastAddresses) {
            if ($entry.Address.AddressFamily -ne 'InterNetwork') { continue }
            $address = ConvertTo-IPv4Number $entry.Address.IPAddressToString
            $mask = ConvertTo-IPv4Number $entry.IPv4Mask.IPAddressToString
            $size = 4294967296L - $mask
            if ($size -gt $MaxHosts + 2) {
                Write-Warning "Skipping wide network on $($adapter.Name); specify -Subnet and -MaxHosts explicitly."
                continue
            }
            $ranges += [pscustomobject]@{
                Network = ($address -band $mask); Size = $size; Label = $adapter.Name
            }
        }
    }
}

$targets = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($range in $ranges) {
    $hostCount = if ($range.Size -gt 2) { $range.Size - 2 } else { $range.Size }
    if ($hostCount -gt $MaxHosts) { throw "Network $($range.Label) exceeds -MaxHosts $MaxHosts." }
    $first = if ($range.Size -gt 2) { $range.Network + 1 } else { $range.Network }
    for ($offset = 0L; $offset -lt $hostCount; $offset++) {
        [void]$targets.Add((ConvertFrom-IPv4Number ($first + $offset)))
        if ($targets.Count -gt $MaxHosts) { throw "Combined networks exceed -MaxHosts $MaxHosts." }
    }
}
if ($targets.Count -eq 0) { throw 'No IPv4 networks found; supply -Subnet explicitly.' }
Write-Host "Checking SSH on $($targets.Count) addresses..."
$addresses = @($targets | Sort-Object)
$candidates = @()
for ($start = 0; $start -lt $addresses.Count; $start += $Parallelism) {
    $pending = @()
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        foreach ($address in $addresses[$start..([Math]::Min($start + $Parallelism - 1, $addresses.Count - 1))]) {
            $client = New-Object System.Net.Sockets.TcpClient
            try {
                $connection = $client.BeginConnect($address, 22, $null, $null)
                $pending += [pscustomobject]@{ Address = $address; Client = $client; Connection = $connection }
            } catch { $client.Close() }
        }
        foreach ($probe in $pending) {
            $remaining = [Math]::Max(0, $TimeoutMs - [int]$clock.ElapsedMilliseconds)
            if ($probe.Connection.AsyncWaitHandle.WaitOne($remaining)) {
                try {
                    $probe.Client.EndConnect($probe.Connection)
                    if ($probe.Client.Connected) { $candidates += $probe.Address }
                } catch { }
            }
        }
    } finally {
        foreach ($probe in $pending) {
            $probe.Client.Close()
            $probe.Connection.AsyncWaitHandle.Close()
        }
    }
}

$found = 0
foreach ($address in $candidates) {
    Write-Host "Verifying $address via SSH..."
    $options = @('-i', $IdentityFile, '-o', 'IdentitiesOnly=yes', '-o', 'BatchMode=yes',
        '-o', 'ConnectTimeout=5', '-o', 'ConnectionAttempts=1', '-o', 'StrictHostKeyChecking=accept-new')
    $remoteCommand = 'if test -r /proc/device-tree/model; then head -c -1 /proc/device-tree/model; echo; hostname; uname -m; fi'
    $savedErrorAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & $ssh @options "${UserName}@${address}" $remoteCommand 2>$null
        $sshExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedErrorAction
    }
    if ($sshExit -ne 0) {
        Write-Warning "SSH candidate $address could not be authenticated; not confirmed as Raspberry Pi."
        continue
    }
    $lines = @($output)
    if ($lines.Count -ge 3 -and $lines[0] -match '^Raspberry Pi') {
        $found++
        [pscustomobject]@{
            Address = $address; SshTarget = "${UserName}@${address}"
            Model = $lines[0]; HostName = $lines[1]; Architecture = $lines[2]
        }
    }
}
if ($found -eq 0) {
    throw "No Raspberry Pi confirmed. SSH candidates: $($candidates -join ', '). Check power, network, user and key."
}