param(
    [string[]]$Paths = @('/devices'),
    [ValidateRange(0, 8)][int]$Depth = 0,
    [ValidateRange(1, 4096)][int]$MaxRequests = 256,
    [string]$CaptureDirectory = ''
)

# Private, read-only research tool. It has no set, keepalive-write, or UI API.
$ErrorActionPreference = 'Stop'
$captureFile = $null
if ($CaptureDirectory) {
    $repository = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
    $destination = [IO.Path]::GetFullPath($CaptureDirectory).TrimEnd('\')
    if ($destination -ieq $repository -or $destination.StartsWith($repository + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Raw Apollo captures must be outside the repository.'
    }
    [IO.Directory]::CreateDirectory($destination) | Out-Null
    $captureFile = Join-Path $destination ('apollo-read-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '.json')
}

function Read-ApolloNode([string]$NodePath) {
    if ($NodePath -cnotmatch '^/(?:[A-Za-z0-9_.-]+/)*[A-Za-z0-9_.-]*$' -or $NodePath.Contains('..') -or $NodePath.Length -gt 2048) {
        throw 'Invalid Apollo path.'
    }
    $client = [Net.Sockets.TcpClient]::new()
    try {
        $connecting = $client.ConnectAsync('127.0.0.1', 4710)
        if (-not $connecting.Wait(1500)) { throw 'Apollo connection timed out.' }
        $stream = $client.GetStream()
        $stream.ReadTimeout = 2000
        $stream.WriteTimeout = 2000
        $request = [Text.Encoding]::UTF8.GetBytes("get $NodePath`0")
        $stream.Write($request, 0, $request.Length)
        $bytes = [Collections.Generic.List[byte]]::new()
        $buffer = [byte[]]::new(8192)
        $deadline = [Diagnostics.Stopwatch]::StartNew()
        $complete = $false
        while (-not $complete) {
            if ($deadline.ElapsedMilliseconds -ge 5000) { throw 'Apollo response deadline exceeded.' }
            $count = $stream.Read($buffer, 0, $buffer.Length)
            if ($count -eq 0) { throw 'Apollo closed an incomplete frame.' }
            for ($i = 0; $i -lt $count; ++$i) {
                if ($buffer[$i] -eq 0) { $complete = $true; break }
                $bytes.Add($buffer[$i])
                if ($bytes.Count -gt 4194304) { throw 'Apollo response exceeds 4 MiB.' }
            }
        }
        $response = [Text.UTF8Encoding]::new($false, $true).GetString($bytes.ToArray()) | ConvertFrom-Json
        if ($response.path -cne $NodePath) { throw 'Apollo response path does not match request.' }
        return $response
    } finally { $client.Dispose() }
}

$queue = [Collections.Generic.Queue[object]]::new()
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$records = [Collections.Generic.List[object]]::new()
foreach ($nodePath in $Paths) { $queue.Enqueue(@{ Path = $nodePath; Depth = 0 }) }
try {
    while ($queue.Count -gt 0 -and $records.Count -lt $MaxRequests) {
        $item = $queue.Dequeue()
        if (-not $seen.Add($item.Path)) { continue }
        $response = Read-ApolloNode $item.Path
        $records.Add($response)
        $propertyNames = @($response.data.properties.PSObject.Properties | ForEach-Object { $_.Name })
        $childNames = @($response.data.children.PSObject.Properties | ForEach-Object { $_.Name })
        Write-Host ($item.Path + ' | properties: ' + ($propertyNames -join ', ') + ' | children: ' + ($childNames -join ', '))
        if ($item.Depth -lt $Depth) {
            foreach ($child in $childNames) {
                if ($child -cmatch '^[A-Za-z0-9_.-]+$' -and -not $child.Contains('..')) {
                    $queue.Enqueue(@{ Path = $item.Path.TrimEnd('/') + '/' + $child; Depth = $item.Depth + 1 })
                }
            }
        }
        Start-Sleep -Milliseconds 10
    }
} finally {
    if ($captureFile) {
        $report = [ordered]@{ schema = 1; readOnly = $true; utc = [DateTime]::UtcNow.ToString('o'); pendingNodes = $queue.Count; records = $records.ToArray() }
        # Runtime capture, not a source-file edit. Never place this data in Git.
        [IO.File]::WriteAllText($captureFile, ($report | ConvertTo-Json -Depth 64), [Text.UTF8Encoding]::new($false))
        Write-Host "Private capture: $captureFile"
    }
}
Write-Host "Read $($records.Count) nodes; $($queue.Count) not explored. No state writes sent."
