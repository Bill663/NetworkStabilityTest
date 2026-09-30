param(
  [int]$DurationMinutes = 60,
  [int]$DurationSeconds = 0,
  [string]$WorkspaceRoot = (Get-Location).Path,
  [string]$StopFile = '',
  [string]$RunId = ''
)

$ErrorActionPreference = 'Stop'
$work = Join-Path $WorkspaceRoot 'work'
$out = Join-Path $WorkspaceRoot 'outputs'
New-Item -ItemType Directory -Force -Path $work, $out | Out-Null

$timestamp = if ($RunId) { $RunId } else { Get-Date -Format 'yyyyMMdd-HHmmss' }
if ($DurationSeconds -le 0) {
  $DurationSeconds = [math]::Max(1, $DurationMinutes) * 60
}
$durationLabel = if (($DurationSeconds % 60) -eq 0) { "$([int]($DurationSeconds / 60))min" } else { "${DurationSeconds}s" }
$csv = Join-Path $work "router-and-network-$durationLabel-$timestamp.csv"
$summary = Join-Path $out "router-and-network-$durationLabel-report-$timestamp.txt"
$progressPath = Join-Path $work "router-and-network-$durationLabel-progress-$timestamp.json"

$gateway = $null
try {
  $gateway = (Get-NetIPConfiguration |
    Where-Object { $_.IPv4DefaultGateway -and $_.NetAdapter.Status -eq 'Up' } |
    Select-Object -First 1 -ExpandProperty IPv4DefaultGateway).NextHop
} catch {}

if (-not $gateway) {
  try {
    # Route table access works in restricted sessions where adapter CIM queries may be denied.
    $defaultRoutes = @(& route.exe print -4 2>$null | ForEach-Object {
      if ($_ -match '^\s*0\.0\.0\.0\s+0\.0\.0\.0\s+(\d{1,3}(?:\.\d{1,3}){3})\s+\S+\s+(\d+)\s*$') {
        [pscustomobject]@{ Gateway = $Matches[1]; Metric = [int]$Matches[2] }
      }
    })
    $gateway = ($defaultRoutes | Sort-Object Metric | Select-Object -First 1 -ExpandProperty Gateway)
  } catch {}
}

function Test-StopRequested {
  return ($StopFile -and (Test-Path -LiteralPath $StopFile))
}

function Invoke-TcpProbe($targetHost, $port, $timeoutMs) {
  $client = [System.Net.Sockets.TcpClient]::new()
  $sw = [Diagnostics.Stopwatch]::StartNew()
  try {
    $iar = $client.BeginConnect($targetHost, $port, $null, $null)
    $ok = $iar.AsyncWaitHandle.WaitOne($timeoutMs, $false)
    if (-not $ok) { return @{ Success = $false; LatencyMs = ''; Status = 'Timeout' } }
    $client.EndConnect($iar)
    $sw.Stop()
    return @{ Success = $true; LatencyMs = [int]$sw.ElapsedMilliseconds; Status = 'Connected' }
  } catch {
    return @{ Success = $false; LatencyMs = ''; Status = ($_.Exception.Message -replace '"', '''') -replace '[\r\n]+', ' ' }
  } finally {
    $client.Close()
  }
}

function Invoke-DnsProbe($targetHost, $timeoutMs) {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  try {
    $task = [System.Net.Dns]::GetHostAddressesAsync($targetHost)
    if (-not $task.Wait($timeoutMs)) { return @{ Success = $false; LatencyMs = ''; Status = 'Timeout' } }
    $addresses = @($task.Result | ForEach-Object { $_.IPAddressToString })
    $sw.Stop()
    if ($addresses.Count -gt 0) {
      return @{ Success = $true; LatencyMs = [int]$sw.ElapsedMilliseconds; Status = ($addresses -join ';') }
    }
    return @{ Success = $false; LatencyMs = ''; Status = 'NoAddresses' }
  } catch {
    return @{ Success = $false; LatencyMs = ''; Status = ($_.Exception.Message -replace '"', '''') -replace '[\r\n]+', ' ' }
  }
}

function New-EmptyStats($name, $layer) {
  [ordered]@{
    name = $name
    layer = $layer
    sent = 0
    ok = 0
    failed = 0
    failurePct = 0
    minMs = $null
    avgMs = $null
    maxMs = $null
    lastStatus = ''
    lastUpdatedAt = ''
  }
}

function Update-ProbeStats($probe, $success, $latencyMs, $status) {
  if (-not $script:probeStats.Contains($probe.Name)) {
    $script:probeStats[$probe.Name] = New-EmptyStats $probe.Name $probe.Layer
  }
  if (-not $script:layerStats.Contains($probe.Layer)) {
    $script:layerStats[$probe.Layer] = New-EmptyStats $probe.Layer $probe.Layer
  }

  foreach ($stats in @($script:probeStats[$probe.Name], $script:layerStats[$probe.Layer])) {
    $stats.sent++
    if ($success) {
      $stats.ok++
      if ($latencyMs -ne '') {
        $latencyNumber = [double]$latencyMs
        if ($null -eq $stats.minMs -or $latencyNumber -lt $stats.minMs) { $stats.minMs = $latencyNumber }
        if ($null -eq $stats.maxMs -or $latencyNumber -gt $stats.maxMs) { $stats.maxMs = $latencyNumber }
        if ($null -eq $stats.avgMs) {
          $stats.avgMs = $latencyNumber
        } else {
          $stats.avgMs = (($stats.avgMs * ($stats.ok - 1)) + $latencyNumber) / $stats.ok
        }
      }
    } else {
      $stats.failed++
    }
    $stats.failurePct = if ($stats.sent -gt 0) { [math]::Round(($stats.failed / $stats.sent) * 100, 3) } else { 0 }
    if ($null -ne $stats.avgMs) { $stats.avgMs = [math]::Round($stats.avgMs, 2) }
    if ($null -ne $stats.minMs) { $stats.minMs = [math]::Round($stats.minMs, 2) }
    if ($null -ne $stats.maxMs) { $stats.maxMs = [math]::Round($stats.maxMs, 2) }
    $stats.lastStatus = $status
    $stats.lastUpdatedAt = (Get-Date).ToString('o')
  }
}

function Write-ProgressFile($state, $elapsedSeconds, $csvPath, $summaryPath, $progressFile, $probeCount) {
  $progressPct = [math]::Min(100, [math]::Round(($elapsedSeconds / $DurationSeconds) * 100, 2))
  $json = [pscustomobject]@{
    state = $state
    elapsedSeconds = [math]::Round($elapsedSeconds, 2)
    durationSeconds = $DurationSeconds
    durationMinutes = [math]::Round($DurationSeconds / 60, 2)
    progressPct = $progressPct
    probeCount = $probeCount
    csv = $csvPath
    summary = $summaryPath
    gateway = $gateway
    probes = @($script:probeStats.Values)
    layers = @($script:layerStats.Values)
    updatedAt = (Get-Date).ToString('o')
  } | ConvertTo-Json -Depth 6
  Write-AtomicTextWithRetry $progressFile $json
}

function Write-AtomicTextWithRetry($path, $text) {
  $tempPath = "$path.$PID.tmp"
  for ($attempt = 1; $attempt -le 5; $attempt++) {
    try {
      [IO.File]::WriteAllText($tempPath, $text, [Text.UTF8Encoding]::new($false))
      Move-Item -LiteralPath $tempPath -Destination $path -Force
      return
    } catch {
      if ($attempt -eq 5) { throw }
      Start-Sleep -Milliseconds (100 * $attempt)
    } finally {
      if (Test-Path -LiteralPath $tempPath) {
        Remove-Item -LiteralPath $tempPath -Force -ErrorAction SilentlyContinue
      }
    }
  }
}

function Write-TextWithRetry($path, $text, $append) {
  for ($attempt = 1; $attempt -le 5; $attempt++) {
    try {
      if ($append) {
        Add-Content -Path $path -Value $text -Encoding UTF8
      } else {
        Set-Content -Path $path -Value $text -Encoding UTF8
      }
      return
    } catch [System.IO.IOException] {
      if ($attempt -eq 5) { throw }
      Start-Sleep -Milliseconds (100 * $attempt)
    }
  }
}

Write-TextWithRetry $csv 'DateTime,ElapsedSeconds,Layer,ProbeName,Target,Success,LatencyMs,Status' $false
$duration = [TimeSpan]::FromSeconds($DurationSeconds)
$swRun = [Diagnostics.Stopwatch]::StartNew()
$pinger = [System.Net.NetworkInformation.Ping]::new()
$probeCount = 0
$script:probeStats = [ordered]@{}
$script:layerStats = [ordered]@{}
Write-ProgressFile 'running' 0 $csv $summary $progressPath 0

try {
  while ($swRun.Elapsed -lt $duration -and -not (Test-StopRequested)) {
    $probes = @()
    if ($gateway) { $probes += @{ Layer = 'router'; Name = 'router-ping'; Kind = 'ping'; Target = $gateway } }
    $probes += @{ Layer = 'network'; Name = 'cloudflare-ping'; Kind = 'ping'; Target = '1.1.1.1' }
    $probes += @{ Layer = 'network'; Name = 'google-ping'; Kind = 'ping'; Target = '8.8.8.8' }
    $probes += @{ Layer = 'network'; Name = 'dns-google'; Kind = 'dns'; Target = 'www.google.com' }
    $probes += @{ Layer = 'network'; Name = 'dns-cloudflare'; Kind = 'dns'; Target = 'www.cloudflare.com' }
    $probes += @{ Layer = 'network'; Name = 'tcp-cloudflare-443'; Kind = 'tcp'; Target = '1.1.1.1:443' }
    $probes += @{ Layer = 'network'; Name = 'tcp-google-443'; Kind = 'tcp'; Target = 'www.google.com:443' }

    foreach ($probe in $probes) {
      if ((Test-StopRequested) -or $swRun.Elapsed -ge $duration) { break }
      $now = Get-Date
      $success = $false
      $latency = ''
      $status = 'Unknown'
      if ($probe.Kind -eq 'ping') {
        try {
          $reply = $pinger.Send($probe.Target, 2000)
          $status = [string]$reply.Status
          if ($reply.Status -eq [System.Net.NetworkInformation.IPStatus]::Success) {
            $success = $true
            $latency = [int]$reply.RoundtripTime
          }
        } catch {
          $status = ($_.Exception.Message -replace '"', '''') -replace '[\r\n]+', ' '
        }
      } elseif ($probe.Kind -eq 'dns') {
        $result = Invoke-DnsProbe $probe.Target 2000
        $success = $result.Success
        $latency = $result.LatencyMs
        $status = $result.Status
      } elseif ($probe.Kind -eq 'tcp') {
        $parts = $probe.Target.Split(':')
        $result = Invoke-TcpProbe $parts[0] ([int]$parts[1]) 2000
        $success = $result.Success
        $latency = $result.LatencyMs
        $status = $result.Status
      }
      $row = '"{0}",{1},"{2}","{3}","{4}",{5},"{6}","{7}"' -f $now.ToString('o'), [math]::Round($swRun.Elapsed.TotalSeconds, 3), $probe.Layer, $probe.Name, $probe.Target, $success.ToString().ToLowerInvariant(), $latency, (($status -replace '"', '''') -replace '[\r\n]+', ' ')
      Write-TextWithRetry $csv $row $true
      Update-ProbeStats $probe $success $latency $status
      $probeCount++
    }
    Write-ProgressFile 'running' $swRun.Elapsed.TotalSeconds $csv $summary $progressPath $probeCount
    if ((Test-StopRequested) -or $swRun.Elapsed -ge $duration) { break }
    Start-Sleep -Seconds 2
  }
} finally {
  $swRun.Stop()
  $pinger.Dispose()
}

$data = Import-Csv $csv
$lines = [System.Collections.Generic.List[string]]::new()
$finalState = if ((Test-StopRequested) -and $swRun.Elapsed -lt $duration) { 'stopped' } else { 'complete' }
$allLayerStats = @($script:layerStats.Values)
$totalSent = 0
$totalFailed = 0
foreach ($layerStat in $allLayerStats) {
  $totalSent += [int]$layerStat['sent']
  $totalFailed += [int]$layerStat['failed']
}
$overallFailurePct = if ($totalSent -gt 0) { ($totalFailed / $totalSent) * 100 } else { 0 }
$availabilityScore = [math]::Max(0, 100 - [math]::Min(100, $overallFailurePct * 8))
$networkAverageMs = if ($script:layerStats.Contains('network')) { $script:layerStats['network'].avgMs } else { $null }
$latencyScore = if ($null -eq $networkAverageMs) {
  70
} elseif ($networkAverageMs -le 40) {
  100
} elseif ($networkAverageMs -le 80) {
  85
} elseif ($networkAverageMs -le 150) {
  65
} elseif ($networkAverageMs -le 300) {
  40
} else {
  15
}
$networkScore = [int][math]::Round(($availabilityScore * 0.70) + ($latencyScore * 0.30))
$networkGrade = if ($networkScore -ge 90) {
  'Excellent'
} elseif ($networkScore -ge 75) {
  'Good'
} elseif ($networkScore -ge 55) {
  'Fair'
} elseif ($networkScore -ge 35) {
  'Poor'
} else {
  'Critical'
}
$lines.Add("Router And Network Stability Report - $durationLabel Test")
$lines.Add("Generated: $(Get-Date -Format o)")
$lines.Add("Duration observed: $([math]::Round($swRun.Elapsed.TotalMinutes, 2)) minutes")
$lines.Add("State: $finalState")
$lines.Add("Network rating: $networkScore/100 ($networkGrade)")
$lines.Add("Packet failure: $([math]::Round($overallFailurePct, 2))%; average network latency: $(if ($null -eq $networkAverageMs) { 'Unavailable' } else { "$networkAverageMs ms" })")
$lines.Add("Log CSV: $csv")
$lines.Add("Router gateway: $(if ($gateway) { $gateway } else { 'Not detected' })")
$lines.Add('')
$lines.Add('Per-probe summary:')
foreach ($group in ($data | Group-Object ProbeName)) {
  $rows = @($group.Group)
  $sent = $rows.Count
  $ok = @($rows | Where-Object { $_.Success -eq 'true' })
  $fail = $sent - $ok.Count
  $failure = if ($sent) { [math]::Round(($fail / $sent) * 100, 3) } else { 0 }
  $latencies = @($ok | Where-Object { $_.LatencyMs -ne '' } | ForEach-Object { [double]$_.LatencyMs })
  if ($latencies.Count -gt 0) {
    $avg = [math]::Round(($latencies | Measure-Object -Average).Average, 2)
    $min = [math]::Round(($latencies | Measure-Object -Minimum).Minimum, 2)
    $max = [math]::Round(($latencies | Measure-Object -Maximum).Maximum, 2)
    $sorted = @($latencies | Sort-Object)
    $p95Index = [math]::Min($sorted.Count - 1, [math]::Ceiling($sorted.Count * 0.95) - 1)
    $p95 = [math]::Round($sorted[$p95Index], 2)
    $lines.Add(("- {0}: sent={1}, ok={2}, failed={3}, failure={4}%, latency ms min/avg/p95/max={5}/{6}/{7}/{8}" -f $group.Name, $sent, $ok.Count, $fail, $failure, $min, $avg, $p95, $max))
  } else {
    $lines.Add(("- {0}: sent={1}, ok={2}, failed={3}, failure={4}%, no latency samples" -f $group.Name, $sent, $ok.Count, $fail, $failure))
  }
}
$lines.Add('')
$lines.Add('Layer summary:')
foreach ($group in ($data | Group-Object Layer)) {
  $rows = @($group.Group)
  $ok = @($rows | Where-Object { $_.Success -eq 'true' })
  $fail = $rows.Count - $ok.Count
  $failure = if ($rows.Count) { [math]::Round(($fail / $rows.Count) * 100, 3) } else { 0 }
  $lines.Add(("- {0}: probes={1}, failed={2}, failure={3}%" -f $group.Name, $rows.Count, $fail, $failure))
}
$lines.Add('')
$failures = @($data | Where-Object { $_.Success -ne 'true' })
if ($failures.Count -eq 0) {
  $lines.Add('Failure windows: no failed probes recorded.')
} else {
  $lines.Add('Failure windows:')
  foreach ($group in ($failures | Group-Object ProbeName)) {
    $first = ($group.Group | Select-Object -First 1).DateTime
    $last = ($group.Group | Select-Object -Last 1).DateTime
    $statuses = ($group.Group | Group-Object Status | Sort-Object Count -Descending | ForEach-Object { "$($_.Name)=$($_.Count)" }) -join ', '
    $lines.Add(("- {0}: {1} failed probes, first={2}, last={3}, statuses: {4}" -f $group.Name, $group.Count, $first, $last, $statuses))
  }
}
$lines.Add('')
$routerFails = @($data | Where-Object { $_.Layer -eq 'router' -and $_.Success -ne 'true' }).Count
$networkFails = @($data | Where-Object { $_.Layer -eq 'network' -and $_.Success -ne 'true' }).Count
$lines.Add('Quick read:')
if (-not $gateway -and $networkFails -gt 0) {
  $lines.Add('- No default IPv4 gateway was detected, so the router was not tested directly. Upstream failures were observed, but this run cannot isolate them to the local link, router, DNS, ISP, or remote service.')
} elseif (-not $gateway) {
  $lines.Add('- No default IPv4 gateway was detected, so the router was not tested directly. The available upstream checks were clean during this run.')
} elseif ($routerFails -gt 0) {
  $lines.Add('- Router/gateway failures were observed. That points to the local link, Wi-Fi/Ethernet, or router path.')
} elseif ($networkFails -gt 0) {
  $lines.Add('- Router checks were clean but upstream network checks had failures. That points past the router, DNS, ISP, or remote service reachability.')
} else {
  $lines.Add('- Router and upstream network checks were clean during this run.')
}

Write-TextWithRetry $summary ($lines -join [Environment]::NewLine) $false
Write-ProgressFile $finalState $swRun.Elapsed.TotalSeconds $csv $summary $progressPath $probeCount
Get-Content $summary
