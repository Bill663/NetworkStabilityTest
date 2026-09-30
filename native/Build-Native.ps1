param(
  [ValidateSet('all', 'x64', 'x86', 'arm64')]
  [string]$Architecture = 'all',
  [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$native = $PSScriptRoot
$compilerDir = Join-Path $root '.tools\zig-0.16.0'
if (-not (Test-Path -LiteralPath $compilerDir)) {
  $archive = Join-Path $root '.tools\zig-x86_64-windows-0.16.0.zip'
  if (-not (Test-Path -LiteralPath $archive)) { throw 'Bundled Zig compiler archive is missing.' }
  $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
  if ($hash -ne '68659eb5f1e4eb1437a722f1dd889c5a322c9954607f5edcf337bc3684a75a7e') {
    throw 'Bundled compiler checksum mismatch.'
  }
  Expand-Archive -LiteralPath $archive -DestinationPath $compilerDir
}
$zig = Get-ChildItem -LiteralPath $compilerDir -Filter zig.exe -File -Recurse |
  Select-Object -First 1 -ExpandProperty FullName

if (-not $zig) {
  throw 'Zig 0.16.0 was not found under .tools\zig-0.16.0.'
}

$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root '.tools\zig-cache-global'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root '.tools\zig-cache-local'
New-Item -ItemType Directory -Force -Path $env:ZIG_GLOBAL_CACHE_DIR, $env:ZIG_LOCAL_CACHE_DIR | Out-Null

$buildRoot = Join-Path $native 'build'
if ($Clean -and (Test-Path -LiteralPath $buildRoot)) {
  $resolved = (Resolve-Path -LiteralPath $buildRoot).Path
  if (-not $resolved.StartsWith($native, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean unexpected path: $resolved"
  }
  Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null

$resource = Join-Path $buildRoot 'app.res'
Push-Location (Join-Path $native 'resources')
try {
  & $zig rc /nologo /fo $resource app.rc
  if ($LASTEXITCODE -ne 0) { throw "Resource compilation failed with exit code $LASTEXITCODE" }
} finally {
  Pop-Location
}

$targets = @{
  x64 = 'x86_64-windows-gnu'
  x86 = 'x86-windows-gnu'
  arm64 = 'aarch64-windows-gnu'
}
$selected = if ($Architecture -eq 'all') { @('x64', 'x86', 'arm64') } else { @($Architecture) }

foreach ($name in $selected) {
  $outDir = Join-Path $buildRoot $name
  New-Item -ItemType Directory -Force -Path $outDir | Out-Null
  $output = Join-Path $outDir 'NetworkStabilityTest.exe'
  Write-Host "Building $name..."
  & $zig cc `
    -target $targets[$name] `
    -std=c11 -O2 -Wall -Wextra `
    -municode '-Wl,--subsystem,windows' `
    (Join-Path $native 'src\network_stability_test.c') `
    $resource `
    -o $output `
    -lcomctl32 -lgdi32 -luser32 -lshell32 -lole32 -lws2_32 -liphlpapi -lwlanapi
  if ($LASTEXITCODE -ne 0) { throw "$name build failed with exit code $LASTEXITCODE" }
  Write-Host "Created $output"
}
