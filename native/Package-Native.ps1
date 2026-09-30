param(
  [string]$Version = '2.0.0'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$native = $PSScriptRoot
$build = Join-Path $native 'build'
$dist = Join-Path $root 'dist'
$stagingRoot = Join-Path $native 'package-staging'
$architectures = @('x64', 'x86', 'arm64')

New-Item -ItemType Directory -Force -Path $dist, $stagingRoot | Out-Null

foreach ($architecture in $architectures) {
  $executable = Join-Path $build "$architecture\NetworkStabilityTest.exe"
  if (-not (Test-Path -LiteralPath $executable)) {
    throw "Missing $architecture executable. Run Build-Native.ps1 first."
  }
  $stage = Join-Path $stagingRoot "NetworkStabilityTest-$Version-$architecture"
  if (Test-Path -LiteralPath $stage) {
    $resolved = (Resolve-Path -LiteralPath $stage).Path
    if (-not $resolved.StartsWith($stagingRoot, [StringComparison]::OrdinalIgnoreCase)) {
      throw "Refusing to replace unexpected path: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
  }
  New-Item -ItemType Directory -Force -Path $stage | Out-Null
  Copy-Item -LiteralPath $executable -Destination (Join-Path $stage 'NetworkStabilityTest.exe')
  Copy-Item -LiteralPath (Join-Path $native 'README.txt') -Destination (Join-Path $stage 'README.txt')

  $zip = Join-Path $dist "NetworkStabilityTest-Native-$Version-$architecture.zip"
  if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
  Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal
  Write-Host "Created $zip"
}

$combinedStage = Join-Path $stagingRoot "NetworkStabilityTest-$Version-All-Windows"
if (Test-Path -LiteralPath $combinedStage) {
  $resolvedCombined = (Resolve-Path -LiteralPath $combinedStage).Path
  if (-not $resolvedCombined.StartsWith($stagingRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to replace unexpected path: $resolvedCombined"
  }
  Remove-Item -LiteralPath $resolvedCombined -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $combinedStage | Out-Null
Copy-Item -LiteralPath (Join-Path $native 'README.txt') -Destination (Join-Path $combinedStage 'README.txt')
foreach ($architecture in $architectures) {
  $target = Join-Path $combinedStage $architecture
  New-Item -ItemType Directory -Force -Path $target | Out-Null
  Copy-Item -LiteralPath (Join-Path $build "$architecture\NetworkStabilityTest.exe") -Destination $target
}
$combinedZip = Join-Path $dist "NetworkStabilityTest-Native-$Version-All-Windows.zip"
if (Test-Path -LiteralPath $combinedZip) { Remove-Item -LiteralPath $combinedZip -Force }
Compress-Archive -LiteralPath $combinedStage -DestinationPath $combinedZip -CompressionLevel Optimal
Write-Host "Created $combinedZip"
