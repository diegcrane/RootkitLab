[CmdletBinding()]
param(
    [string]$DriverPath = (Join-Path $PSScriptRoot '..\out\Debug\x64\RootkitLabFilter.sys'),
    [string]$Subject = 'RootkitLab Academic Test Certificate',
    [Parameter(Mandatory=$true)][string]$SignToolPath
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $DriverPath -PathType Leaf)) { throw "Driver not found: $DriverPath" }
if (-not (Test-Path -LiteralPath $SignToolPath -PathType Leaf)) { throw "signtool.exe not found: $SignToolPath" }
& $SignToolPath sign /v /fd SHA256 /sm /s My /n $Subject $DriverPath
if ($LASTEXITCODE -ne 0) { throw "signtool sign failed with exit code $LASTEXITCODE" }
& $SignToolPath verify /v /pa $DriverPath
if ($LASTEXITCODE -ne 0) { throw "signtool verify failed with exit code $LASTEXITCODE" }
Get-FileHash -Algorithm SHA256 -LiteralPath $DriverPath
