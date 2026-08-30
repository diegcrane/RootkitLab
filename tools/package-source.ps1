[CmdletBinding()]
param(
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\..\RootkitLab-v2.0-source.zip')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$allowedParent = (Resolve-Path -LiteralPath (Join-Path $repositoryRoot '..')).Path
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputPath)

if ((Split-Path -Parent $resolvedOutput) -ne $allowedParent -or
    (Split-Path -Leaf $resolvedOutput) -notmatch '^RootkitLab-v[0-9]+\.[0-9]+-source\.zip$') {
    throw "Unexpected source archive path: $resolvedOutput"
}

$items = Get-ChildItem -LiteralPath $repositoryRoot -Force |
    Where-Object Name -notin '.git', 'out'
if ($items.Count -eq 0) {
    throw "No source files were found under $repositoryRoot"
}

Compress-Archive -LiteralPath $items.FullName -DestinationPath $resolvedOutput -CompressionLevel Optimal -Force

$archive = [System.IO.Compression.ZipFile]::OpenRead($resolvedOutput)
try {
    if ($archive.Entries.Count -eq 0) {
        throw 'The source archive is empty.'
    }
    if (@($archive.Entries | Where-Object FullName -like 'RootkitLab/*').Count -gt 0) {
        throw 'The source archive contains an unexpected RootkitLab/ wrapper directory.'
    }
} finally {
    $archive.Dispose()
}

Get-FileHash -Algorithm SHA256 -LiteralPath $resolvedOutput
