$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$destination = Join-Path $root 'dist\NetworkStabilityTest-Project-Transfer.zip'
New-Item -ItemType Directory -Force -Path (Join-Path $root 'dist') | Out-Null
$files = @(Get-ChildItem -LiteralPath $root -File)
foreach ($folder in @('native', 'NST', 'package', 'dist')) {
    $files += Get-ChildItem -LiteralPath (Join-Path $root $folder) -File -Recurse | Where-Object {
        $_.FullName -notlike '*\native\package-staging\*' -and
        $_.Name -notlike '*Project-Transfer*'
    }
}
$files += Get-Item -LiteralPath (Join-Path $root '.tools\zig-x86_64-windows-0.16.0.zip')
$stream = [IO.File]::Open($destination, [IO.FileMode]::Create)
$archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
        $level = if ($file.Extension -eq '.zip') { [IO.Compression.CompressionLevel]::NoCompression } else { [IO.Compression.CompressionLevel]::Optimal }
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, "NetworkStabilityTest/$relative", $level) | Out-Null
    }
} finally {
    $archive.Dispose()
    $stream.Dispose()
}
$check = [IO.Compression.ZipFile]::OpenRead($destination)
try {
    if ($check.Entries.Count -ne $files.Count) { throw 'Archive file count mismatch.' }
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
        $entry = $check.GetEntry("NetworkStabilityTest/$relative")
        $inputStream = $entry.Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = [BitConverter]::ToString($sha.ComputeHash($inputStream)).Replace('-', '') }
        finally { $inputStream.Dispose(); $sha.Dispose() }
        if ($hash -ne (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash) {
            throw "Archive checksum mismatch: $relative"
        }
    }
} finally { $check.Dispose() }
Write-Host "Verified $($files.Count) files in $destination"
Get-FileHash -LiteralPath $destination -Algorithm SHA256
