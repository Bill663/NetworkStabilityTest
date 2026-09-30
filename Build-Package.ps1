$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$packageRoot = Join-Path $root 'package\NetworkStabilityTest'
$dist = Join-Path $root 'dist'
$zipPath = Join-Path $dist 'NetworkStabilityTest-Windows.zip'
$requiredFiles = @(
  'Launch-NetworkStabilityTest.cmd',
  'package\NetworkStabilityTest\Install.cmd',
  'package\NetworkStabilityTest\Uninstall.cmd',
  'package\NetworkStabilityTest\README.txt',
  'NST\app\server.js',
  'NST\app\public\index.html',
  'NST\app\public\app.js',
  'NST\app\public\styles.css',
  'NST\work\router-network-stability-test.ps1'
)

foreach ($file in $requiredFiles) {
  $path = Join-Path $root $file
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Required package file is missing: $path"
  }
}

New-Item -ItemType Directory -Force -Path $packageRoot, $dist | Out-Null
Remove-Item -LiteralPath (Join-Path $packageRoot 'NST') -Recurse -Force -ErrorAction SilentlyContinue

New-Item -ItemType Directory -Force -Path (Join-Path $packageRoot 'NST\app') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $packageRoot 'NST\work') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $packageRoot 'NST\outputs') | Out-Null

Copy-Item -LiteralPath (Join-Path $root 'NST\app\server.js') -Destination (Join-Path $packageRoot 'NST\app\server.js') -Force
Copy-Item -LiteralPath (Join-Path $root 'NST\app\public') -Destination (Join-Path $packageRoot 'NST\app\public') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $root 'NST\work\router-network-stability-test.ps1') -Destination (Join-Path $packageRoot 'NST\work\router-network-stability-test.ps1') -Force
Copy-Item -LiteralPath (Join-Path $root 'Launch-NetworkStabilityTest.cmd') -Destination (Join-Path $packageRoot 'Launch-NetworkStabilityTest.cmd') -Force

Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath $zipPath -Force

Write-Host "Package built:"
Write-Host $zipPath
