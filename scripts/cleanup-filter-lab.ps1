[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$targets = @('C:\RootkitLabSandbox', 'C:\RootkitLabOutside')

foreach ($target in $targets) {
    if (-not (Test-Path -LiteralPath $target)) {
        continue
    }
    $resolved = (Resolve-Path -LiteralPath $target).Path
    if ($resolved -notin $targets) {
        throw "Unexpected cleanup path: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

[pscustomobject]@{
    SandboxPresent = Test-Path -LiteralPath 'C:\RootkitLabSandbox'
    OutsideControlPresent = Test-Path -LiteralPath 'C:\RootkitLabOutside'
}

